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

import "net/http"
import (
	"encoding/json"
	"io/ioutil"
	"math"
	"net/http/httptest"
	"testing"

	"github.com/dcos/dcos-metrics/producers"
)

var (
	nodeMetricsJSON = `{
		"datapoints":[
			{"name":"system.uptime","value":86799,"unit":"count","timestamp":"2017-04-08T20:37:39.259136472Z", "tags":{"foo":"bar"}},
			{"name":"cpu.cores","value":2,"unit":"count","timestamp":"2017-04-08T20:37:39.259145164Z"},
			{"name":"cpu.total","value":1.32,"unit":"percent","timestamp":"2017-04-08T20:37:39.259145164Z"},
			{"name":"nan.val","value":null,"unit":"count","timestamp":"2017-04-08T20:37:39.259145164Z"}
		],
		"dimensions":{
			"mesos_id":"378922ca-dd97-4802-a1b2-dde2f42d74ac",
			"cluster_id":"cc477141-2c93-4b9f-bf05-ec0e163ce2bd",
			"hostname":"192.168.65.90"
		}
	}`
)

func TestPostMetricsToDatadog(t *testing.T) {
	nodeMetrics := producers.MetricsMessage{}
	if err := json.Unmarshal([]byte(nodeMetricsJSON), &nodeMetrics); err != nil {
		t.Fatal("Bad test fixture; could not unmarshal JSON")
	}

	// We assign NaN here, since JSON cannot contain NaNs.
	nodeMetrics.Datapoints[3].Value = math.NaN()

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer r.Body.Close()
		b, err := ioutil.ReadAll(r.Body)
		if err != nil {
			t.Fatalf("Could not read request body: %s", err)
		}

		var series = &DDSeries{}
		if err := json.Unmarshal(b, &series); err != nil {
			t.Fatal(err)
		}

		if len(series.Series) != 3 {
			t.Fatalf("Expected 3 datapoints, got %d", len(series.Series))
		}

		uptime := series.Series[0]
		if uptime.Points[0][0] != 1491683859 {
			t.Fatalf("Expected unix time of 1491683859, got %v", uptime.Points[0])
		}

		expectedTags := []string{
			// message-level tags
			"mesosId:378922ca-dd97-4802-a1b2-dde2f42d74ac",
			"clusterId:cc477141-2c93-4b9f-bf05-ec0e163ce2bd",
			"hostname:192.168.65.90",
			// metric-level tag
			"foo:bar",
		}

		for _, expectedTag := range expectedTags {
			tagWasFound := false
			for _, tag := range uptime.Tags {
				if expectedTag == tag {
					tagWasFound = true
					break
				}
			}
			if !tagWasFound {
				t.Fatalf("Expected to find tag %s, but it was not present", expectedTag)
			}
		}

		if *uptime.Host != "192.168.65.90" {
			t.Fatalf("Expected to find host 192.168.65.90, found %s", uptime.Host)
		}

	}))

	defer server.Close()
	postMetricsToDatadog(server.URL, []producers.MetricsMessage{nodeMetrics})
}
