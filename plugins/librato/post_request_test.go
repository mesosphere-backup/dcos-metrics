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
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"reflect"
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/producers"
)

var (
	datapointTime = time.Now()
	expectedTime  = time.Unix(datapointTime.Unix()/10*10, 0)
	testMessages  = []producers.MetricsMessage{
		{
			Datapoints: []producers.Datapoint{
				{
					Name: "my-metric",
					Tags: map[string]string{
						"tag1": "value1",
					},
					Value:     42,
					Timestamp: datapointTime.Format(time.RFC3339),
				},
			},
		},
	}
	testLibratoEmail    = "test@example.com"
	testLibratoToken    = "testToken"
	testPollingInterval = int64(10)
	testMetricPrefix    = "dcos"
)

func TestPostRequest(t *testing.T) {
	libratoResponse := &libratoResponse{}
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer r.Body.Close()

		// verify the request that the postRequest sent
		if r.Header.Get("Authorization") != expectedTestAuth() {
			t.Fatal("Authentication header was wrong")
		}
		b, err := ioutil.ReadAll(r.Body)
		if err != nil {
			t.Fatalf("Could not read request body: %v", err)
		}
		var postRequest postRequest
		if err := json.Unmarshal(b, &postRequest); err != nil {
			t.Fatal(err)
		}
		if len(postRequest.Measurements) != 1 {
			t.Fatalf("Should have been 1 measure but instead there were %d", len(postRequest.Measurements))
		}
		measurement := postRequest.Measurements[0]
		if measurement.Value != 42 {
			t.Fatalf("Measure should have been 42 but was %f", measurement.Value)
		}
		if measurement.Name != "dcos.my-metric" {
			t.Fatalf("Measure name should have been 'my-metric' but was %s", measurement.Name)
		}
		// verify that the measure time was floored to the polling interval
		if measurement.Time != expectedTime.Unix() {
			t.Fatalf("Measure time should have been 1000 but was %d", measurement.Time)
		}
		if !reflect.DeepEqual(measurement.Tags, map[string]string{
			"tag1": "value1",
		}) {
			t.Fatalf("Tags were wrong: %v", measurement.Tags)
		}

		// send a response to the postRequest
		b, err = json.Marshal(libratoResponse)
		if err != nil {
			http.Error(w, "Could not serialize: "+err.Error(), http.StatusInternalServerError)
			return
		}
		w.WriteHeader(http.StatusAccepted)
		if _, err := w.Write(b); err != nil {
			t.Fatalf("Could not write response: %s", err)
		}
	}))
	postRequest, err := newTestPostRequest(server)
	if err != nil {
		t.Fatal(err)
	}
	postRequest.add(testMessages)
	if err := postRequest.send(); err != nil {
		t.Fatal(err)
	}
}

func TestPostRequestFailure(t *testing.T) {
	libratoResponse := &libratoResponse{}
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer r.Body.Close()
		// fake an erroneous response
		libratoResponse.addError("test-error")
		b, err := json.Marshal(libratoResponse)
		if err != nil {
			http.Error(w, "Could not serialize: "+err.Error(), http.StatusInternalServerError)
			return
		}
		w.WriteHeader(http.StatusAccepted)
		if _, err := w.Write(b); err != nil {
			t.Fatalf("Could not write response: %s", err)
		}
	}))
	postRequest, err := newTestPostRequest(server)
	if err != nil {
		t.Fatal(err)
	}
	postRequest.add(testMessages)
	if err := postRequest.send(); err == nil {
		t.Fatal(err)
	}
}

func newTestPostRequest(server *httptest.Server) (*postRequest, error) {
	return newPostRequest(&postRequestOpts{
		libratoURL:      server.URL,
		libratoEmail:    testLibratoEmail,
		libratoToken:    testLibratoToken,
		pollingInterval: testPollingInterval,
		metricPrefix:    testMetricPrefix,
	})
}

func expectedTestAuth() string {
	authChunk := fmt.Sprintf("%s:%s", testLibratoEmail, testLibratoToken)
	encoded := base64.StdEncoding.EncodeToString([]byte(authChunk))
	return fmt.Sprintf("Basic %s", string(encoded))
}
