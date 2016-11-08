// Copyright 2016 Mesosphere, Inc.
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

package collector

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
	"time"

	log "github.com/Sirupsen/logrus"
)

var (
	// TODO(roger): place the value of userAgent somewhere convenient
	userAgent = "com.mesosphere.dcos-metrics/1.0"
)

// HTTPClient defines the structure of the HTTP client implementation used for
// collecting metrics from a Mesos master or agent over HTTP.
type HTTPClient struct {
	httpClient *http.Client
	host       string
	path       string
	authToken  string // use an empty string "" to disable auth
}

// NewHTTPClient returns a new instance of the HTTP client.
func NewHTTPClient(host string, path string, timeout time.Duration) *HTTPClient {
	var token string
	// TODO(roger): if auth is required, get a token via jwt/transport. See
	// https://github.com/dcos/dcos-go/blob/59a7af3/jwt/transport/cmd/example/main.go#L32
	//
	// This should be keyed off a configuration option.
	token = ""

	return &HTTPClient{
		httpClient: &http.Client{Timeout: timeout},
		host:       host,
		path:       path,
		authToken:  token,
	}
}

// URL returns the URL for this client as a string.
func (c *HTTPClient) URL() string {
	u := url.URL{Scheme: "http", Host: c.host, Path: c.path}
	return u.String()
}

// Fetch queries an API endpoint and expects to receive JSON. It then unmarshals
// the JSON to the interface{} provided by the caller. By returning data this
// way, Fetch() ensures that JSON is always unmarshaled the same way, and that
// errors are handled correctly, but allows the returned data to be mapped to
// an arbitrary struct that the caller is aware of.
func (c *HTTPClient) Fetch(target interface{}) error {
	log.Debug("Building new HTTP request for ", c.URL())
	req, err := http.NewRequest("GET", c.URL(), nil)
	if err != nil {
		return err
	}

	req.Header.Set("User-Agent", userAgent)

	if c.authToken != "" {
		req.Header.Set("Authorization", fmt.Sprintf("token=%s", c.authToken))
	}

	log.Debug("Fetching data from ", c.URL())
	resp, err := c.httpClient.Do(req)
	if err != nil {
		log.Error(err)
		return err
	}
	defer resp.Body.Close()

	// Special case: on HTTP 401 Unauthorized, exit immediately. Considering this
	// could be running in a goroutine, we don't want the caller to fail forever.
	if resp.StatusCode == http.StatusUnauthorized {
		log.Fatalf("fetch error: unauthorized: please provide a suitable auth token")
	}

	if resp.StatusCode != http.StatusOK {
		e := fmt.Errorf("fetch error: %s", resp.Status)
		log.Error(e)
		return e
	}

	b, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		e := fmt.Errorf("read error: %s: %v\n", c.URL(), err)
		log.Error(e)
		return e
	}

	if err := json.Unmarshal(b, &target); err != nil {
		e := fmt.Errorf("unmarshal error: %s: %v\n", b, err)
		log.Error(e)
		return e
	}

	return nil
}
