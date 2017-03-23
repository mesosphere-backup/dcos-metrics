// Copyright 2017 Mesosphere, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"net/http"
	"strings"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/producers"
)

// retained to take advantage of connection re-use
var httpClient http.Client

func init() {
	httpClient.Timeout = 5 * time.Second
}

// postRequest is the main payload to librato
type postRequest struct {
	Measurements []*measurement `json:"measurements"`
	opts         *postRequestOpts
}

// postRequestOpts are the configurable options for postRequest
type postRequestOpts struct {
	libratoUrl      string
	libratoEmail    string
	libratoToken    string
	pollingInterval int64
}

func newPostRequest(opts *postRequestOpts) (*postRequest, error) {
	isBlank := func(str string) bool {
		return len(strings.TrimSpace(str)) == 0
	}
	if isBlank(opts.libratoUrl) {
		return nil, errors.New("Librato url must be specified")
	}
	if isBlank(opts.libratoEmail) {
		return nil, errors.New("Librato email address must be specified")
	}
	if isBlank(opts.libratoToken) {
		return nil, errors.New("Librato account token must be specified")
	}
	if opts.pollingInterval <= 0 {
		return nil, errors.New("Polling interval must be >= 0")
	}
	pr := &postRequest{
		opts: opts,
	}
	return pr, nil
}

func (p *postRequest) add(messages []producers.MetricsMessage) {
	oldDatapoints := 0
	for _, message := range messages {
		dimensions := message.Dimensions
		for _, datapoint := range message.Datapoints {
			measurement := newMeasurement()
			measurement.Name = datapoint.Name
			timestamp, err := p.parseTimestamp(datapoint.Timestamp)
			if err != nil {
				log.Errorf("Could not parse timestamp '%s': %v", datapoint.Timestamp, err)
				continue
			}
			if timestamp.Before(time.Now().Add(-10 * time.Minute)) {
				log.Debugf("Timestamp '%s' for '%s' is too old", datapoint.Timestamp, datapoint.Name)
				oldDatapoints++
				continue
			}
			measurement.Time = timestamp.Unix()
			measurement.floorTime(p.opts.pollingInterval)
			for k, v := range datapoint.Tags {
				p.setTag(measurement, k, v)
			}
			p.setTag(measurement, "mesosId", dimensions.MesosID)
			p.setTag(measurement, "clusterId", dimensions.ClusterID)
			p.setTag(measurement, "containerId", dimensions.ContainerID)
			p.setTag(measurement, "executorId", dimensions.ExecutorID)
			p.setTag(measurement, "frameworkName", dimensions.FrameworkName)
			p.setTag(measurement, "frameworkId", dimensions.FrameworkID)
			p.setTag(measurement, "frameworkRole", dimensions.FrameworkRole)
			p.setTag(measurement, "frameworkPrincipal", dimensions.FrameworkPrincipal)
			p.setTag(measurement, "hostname", dimensions.Hostname)
			for k, v := range dimensions.Labels {
				p.setTag(measurement, fmt.Sprintf("label:%s", k), v)
			}
			if err := measurement.setValue(datapoint.Value); err != nil {
				log.Errorf("Skipping datapoint '%s' due to an invalid value: %v", measurement, err)
				continue
			}
			if err := measurement.validate(); err != nil {
				log.Errorf("Skipping datapoint '%s' due to a validation error: %v", measurement, err)
				continue
			}
			p.Measurements = append(p.Measurements, measurement)
		}
	}
	if oldDatapoints > 0 {
		log.Warnf("Rejected %d datapoints because they were too old", oldDatapoints)
	}
}

func (p *postRequest) parseTimestamp(timestamp string) (*time.Time, error) {
	if timestamp == "" {
		return nil, errors.New("Received an empty timestamp")
	}
	parsed, err := time.Parse(time.RFC3339, timestamp)
	if err != nil {
		return nil, err
	}
	return &parsed, nil
}

func (p *postRequest) setTag(m *measurement, name string, value string) {
	if len(value) == 0 {
		// don't bother if a tag value was not specified
		return
	}
	if err := m.addTag(name, value); err != nil {
		// some tags cannot be used, this will get noisy if on the warn level
		log.Debugf("Invalid tag '%s'='%s' for measurement '%s': %v", name, value, m, err)
	}
}

func (p *postRequest) floorTime(value int64) int64 {
	if value <= 0 {
		value = time.Now().Unix()
	}
	// floor the timestamp to the polling interval to align datapoints
	return value / p.opts.pollingInterval * p.opts.pollingInterval
}

func (p *postRequest) send() error {
	encoded, err := json.Marshal(p)
	if err != nil {
		return fmt.Errorf("Could not marshal request: %v", err)
	}
	url := p.opts.libratoUrl + "/v1/measurements"
	req, err := http.NewRequest("POST", url, bytes.NewReader(encoded))
	if err != nil {
		return fmt.Errorf("Could not build request: %v", err)
	}
	authHeader, err := p.authHeader()
	if err != nil {
		return fmt.Errorf("Could not create authentication header: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", authHeader)
	resp, err := httpClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	return p.checkResponse(resp)
}

func (p *postRequest) checkResponse(resp *http.Response) error {
	respBody, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("Got response code %d but could not read body: %v", resp.StatusCode, err)
	}
	if resp.StatusCode == http.StatusAccepted {
		var libratoResponse libratoResponse
		if err := json.Unmarshal(respBody, &libratoResponse); err != nil {
			return fmt.Errorf("Got response code %d but could not decode response body: %v", resp.StatusCode, err)
		}
		if libratoResponse.Measurements.Summary.Failed == 0 {
			return nil
		}
	}
	return fmt.Errorf("Librato responded with code: %d and body: %s", resp.StatusCode, string(respBody))
}

func (p *postRequest) authHeader() (string, error) {
	libratoEmail := p.opts.libratoEmail
	libratoToken := p.opts.libratoToken
	if len(strings.TrimSpace(libratoEmail)) == 0 {
		return "", errors.New("The " + libratoEmailFlagName + " flag must be specified.")
	}
	if len(strings.TrimSpace(libratoToken)) == 0 {
		return "", errors.New("The " + libratoTokenFlagName + " flag must be specified.")
	}
	userPass := fmt.Sprintf("%s:%s", libratoEmail, libratoToken)
	base64Encoded := base64.StdEncoding.EncodeToString([]byte(userPass))
	return fmt.Sprintf("Basic %s", base64Encoded), nil
}