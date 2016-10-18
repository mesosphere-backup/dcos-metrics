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

package util

import (
	"fmt"
	"io/ioutil"
	"net/http"
)

const (
	userAgent = "dcos-metrics-collector/1.0"
)

// HTTPCodeError ...
type HTTPCodeError struct {
	Code int
	URI  string
}

func (e HTTPCodeError) Error() string {
	return fmt.Sprintf(
		"Got response code when querying %s: %d", e.URI, e.Code)
}
func newHTTPCodeError(code int, uri string) HTTPCodeError {
	return HTTPCodeError{Code: code, URI: uri}
}

// AuthedHTTPGet ...
func AuthedHTTPGet(endpoint string, authToken string) ([]byte, error) {
	request, err := createAuthedHTTPGetRequest(endpoint, authToken)
	if err != nil {
		return nil, err
	}
	return httpGet(request)
}

// HTTPGet ...
func HTTPGet(endpoint string) ([]byte, error) {
	request, err := createHTTPGetRequest(endpoint)
	if err != nil {
		return nil, err
	}
	return httpGet(request)
}

func httpGet(request *http.Request) ([]byte, error) {
	client := http.Client{}
	response, err := client.Do(request)
	if err != nil {
		return nil, err
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return nil, newHTTPCodeError(response.StatusCode, request.RequestURI)
	}
	return ioutil.ReadAll(response.Body)
}

func createAuthedHTTPGetRequest(endpoint string, authToken string) (*http.Request, error) {
	request, err := createHTTPGetRequest(endpoint)
	if err != nil {
		return nil, err
	}
	// Configure auth header
	request.Header.Set("Authorization", fmt.Sprintf("token=%s", authToken))
	return request, nil
}

func createHTTPGetRequest(endpoint string) (*http.Request, error) {
	request, err := http.NewRequest("GET", endpoint, nil)
	if err != nil {
		return nil, err
	}
	// Configure custom UA header for convenient tracing in mesos logs (or elsewhere)
	request.Header.Set("User-Agent", userAgent)
	return request, nil
}
