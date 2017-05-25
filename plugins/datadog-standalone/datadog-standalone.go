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
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"

	log "github.com/Sirupsen/logrus"
	plugin "github.com/dcos/dcos-metrics/plugins"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/urfave/cli"
)

var (
	pluginFlags = []cli.Flag{
		cli.StringFlag{
			Name:  "datadog-key",
			Usage: "DataDog API Key",
		},
	}
)

// DDDataPoint is a tuple of [UNIX timestamp, value]. This has to use floats
// because the value could be non-integer.
type DDDataPoint [2]float64

// DDMetric represents a collection of data points that we might send or receive
// on one single metric line.
type DDMetric struct {
	Metric string        `json:"metric,omitempty"`
	Points []DDDataPoint `json:"points,omitempty"`
	Type   *string       `json:"type,omitempty"`
	Host   *string       `json:"host,omitempty"`
	Tags   []string      `json:"tags,omitempty"`
	Unit   string        `json:"unit,omitempty"`
}

// DDSeries represents a collection of data points we get when we query for timeseries data
type DDSeries struct {
	Series []DDMetric `json:"series"`
}

// DDResult represents the result from a DataDog API Query
type DDResult struct {
	Status   string   `json:"status,omitempty"`
	Errors   []string `json:"errors,omitempty"`
	Warnings []string `json:"warnings,omitempty"`
}

func main() {
	log.Info("Starting Standalone DataDog DC/OS metrics plugin")
	datadogPlugin, err := plugin.New(
		plugin.Name("standalone-datadog"),
		plugin.ExtraFlags(pluginFlags),
		plugin.ConnectorFunc(datadogConnector))

	if err != nil {
		log.Fatal(err)
	}

	log.Fatal(datadogPlugin.StartPlugin())
}

func datadogConnector(metrics []producers.MetricsMessage, c *cli.Context) error {
	if len(metrics) == 0 {
		log.Info("No messages received from metrics service")
		return nil
	}

	log.Info("Transmitting metrics to DataDog")
	datadogURL := fmt.Sprintf("https://app.datadoghq.com/api/v1/series?api_key=%s", c.String("datadog-key"))

	result, err := postMetricsToDatadog(datadogURL, metrics)
	if err != nil {
		log.Errorf("Unexpected error while processing DataDog response: %s", err)
		return nil
	}

	if len(result.Errors) > 0 {
		log.Error("Encountered errors:")
		for _, err := range result.Errors {
			log.Error(err)
		}
	}
	if len(result.Warnings) > 0 {
		log.Warn("Encountered warnings:")
		for _, wrn := range result.Warnings {
			log.Warn(wrn)
		}
	}
	if result.Status == "ok" {
		log.Info("Successfully transmitted metrics")
	} else if result.Status != "" {
		log.Warnf("Expected status to be ok, actually: %v", result.Status)
	}

	return nil
}

func postMetricsToDatadog(datadogURL string, metrics []producers.MetricsMessage) (*DDResult, error) {
	series := messagesToSeries(metrics)
	b := new(bytes.Buffer)
	err := json.NewEncoder(b).Encode(series)
	if err != nil {
		return nil, fmt.Errorf("Could not encode metrics to JSON: %v", err)
	}

	res, err := http.Post(datadogURL, "application/json; charset=utf-8", b)
	if err != nil {
		return nil, fmt.Errorf("Could not post payload to DataDog: %v", err)
	}

	defer res.Body.Close()
	body, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return nil, fmt.Errorf("Could not read response: %v", err)
	}

	result := DDResult{}
	err = json.Unmarshal(body, &result)
	if err != nil {
		log.Warnf("Could not unmarshal datadog response %s: %s", body, err)
		return nil, err
	}
	return &result, nil
}

func messagesToSeries(messages []producers.MetricsMessage) *DDSeries {
	series := new(DDSeries)

	for _, message := range messages {
		dimensions := &message.Dimensions
		host := &dimensions.Hostname
		messageTags := []string{}
		addMessageTag := func(key, value string) {
			if len(value) > 0 {
				messageTags = append(messageTags, fmt.Sprintf("%s:%s", key, value))
			}
		}

		for name, value := range dimensions.Labels {
			addMessageTag(name, value)
		}
		addMessageTag("mesosId", dimensions.MesosID)
		addMessageTag("clusterId", dimensions.ClusterID)
		addMessageTag("containerId", dimensions.ContainerID)
		addMessageTag("executorId", dimensions.ExecutorID)
		addMessageTag("frameworkName", dimensions.FrameworkName)
		addMessageTag("frameworkId", dimensions.FrameworkID)
		addMessageTag("frameworkRole", dimensions.FrameworkRole)
		addMessageTag("frameworkPrincipal", dimensions.FrameworkPrincipal)
		addMessageTag("hostname", dimensions.Hostname)

		for _, datapoint := range message.Datapoints {
			m, err := datapointToDDMetric(datapoint, messageTags, host)
			if err != nil {
				log.Error(err)
				continue
			}
			series.Series = append(series.Series, *m)
		}
	}

	return series
}

func datapointToDDMetric(datapoint producers.Datapoint, messageTags []string, host *string) (*DDMetric, error) {
	t, err := plugin.ParseDatapointTimestamp(datapoint.Timestamp)
	if err != nil {
		return nil, fmt.Errorf("Could not parse timestamp '%s': %v", datapoint.Timestamp, err)
	}

	v, err := plugin.DatapointValueToFloat64(datapoint.Value)
	if err != nil {
		return nil, err
	}

	datapointTags := []string{}
	addDatapointTag := func(key, value string) {
		datapointTags = append(datapointTags, fmt.Sprintf("%s:%s", key, value))
	}
	for name, value := range datapoint.Tags {
		addDatapointTag(name, value)
	}

	metric := DDMetric{
		Metric: datapoint.Name,
		Points: []DDDataPoint{{float64(t.Unix()), v}},
		Tags:   append(messageTags, datapointTags...),
		Unit:   datapoint.Unit,
		Host:   host,
	}
	return &metric, nil
}
