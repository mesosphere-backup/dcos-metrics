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
	"fmt"
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
	taskMetricsJSON = `{
		"datapoints": [
			{"name": "statsd_tester.time.uptime", "value": 73101, "unit": "", "timestamp": "2018-03-08T18:02:52Z", "tags": { "test_tag_key": "test_tag_value" }},
			{"name": "dcos.metrics.module.container_throttled_bytes_per_sec", "value": 0, "unit": "", "timestamp": "2018-03-08T18:02:42Z"},
			{"name": "dcos.metrics.module.container_received_bytes_per_sec", "value": 61.7833, "unit": "", "timestamp": "2018-03-08T18:02:42Z" }
		],
		"dimensions": {
			"mesos_id":"378922ca-dd97-4802-a1b2-dde2f42d74ac",
			"cluster_id":"cc477141-2c93-4b9f-bf05-ec0e163ce2bd",
			"hostname":"192.168.65.90",
			"container_id": "22b0db5b-eef1-4d44-8472-23d088e215da",
			"executor_id": "statsd-emitter.befadcf7-22fa-11e8-bea1-ee89f1bc37fb",
			"framework_id": "379eb87f-403a-4dbf-9a61-722fa2b79e3d-0001",
			"task_name": "statsd-emitter",
			"task_id": "statsd-emitter.befadcf7-22fa-11e8-bea1-ee89f1bc37fb"
		}
	}`
)

func TestPostMetricsToDatadog(t *testing.T) {
	nodeMetrics := producers.MetricsMessage{}
	taskMetrics := producers.MetricsMessage{}
	if err := json.Unmarshal([]byte(nodeMetricsJSON), &nodeMetrics); err != nil {
		t.Fatal("Bad test fixture; could not unmarshal JSON")
	}
	if err := json.Unmarshal([]byte(taskMetricsJSON), &taskMetrics); err != nil {
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

		if len(series.Series) != 6 {
			t.Fatalf("Expected 6 datapoints, got %d", len(series.Series))
		}

		uptime := series.Series[0]
		if uptime.Points[0][0] != 1491683859 {
			t.Fatalf("Expected unix time of 1491683859, got %v", uptime.Points[0])
		}

		expectedNodeTags := []string{
			// message-level tags
			"mesosId:378922ca-dd97-4802-a1b2-dde2f42d74ac",
			"clusterId:cc477141-2c93-4b9f-bf05-ec0e163ce2bd",
			"hostname:192.168.65.90",
			// metric-level tag
			"foo:bar",
		}

		err = compareTags(expectedNodeTags, uptime.Tags)
		if err != nil {
			t.Fatal(err)
		}

		if *uptime.Host != "192.168.65.90" {
			t.Fatalf("Expected to find host 192.168.65.90, found %s", uptime.Host)
		}

		statsdUptime := series.Series[4]
		expectedTaskTags := []string{
			"mesosId:378922ca-dd97-4802-a1b2-dde2f42d74ac",
			"clusterId:cc477141-2c93-4b9f-bf05-ec0e163ce2bd",
			"hostname:192.168.65.90",
			"containerId:22b0db5b-eef1-4d44-8472-23d088e215da",
			"executorId:statsd-emitter.befadcf7-22fa-11e8-bea1-ee89f1bc37fb",
			"frameworkId:379eb87f-403a-4dbf-9a61-722fa2b79e3d-0001",
			"taskName:statsd-emitter",
			"taskId:statsd-emitter.befadcf7-22fa-11e8-bea1-ee89f1bc37fb",
		}

		err = compareTags(expectedTaskTags, statsdUptime.Tags)
		if err != nil {
			t.Fatal(err)
		}

	}))

	defer server.Close()
	postMetricsToDatadog(server.URL, []producers.MetricsMessage{nodeMetrics, taskMetrics})
}

func compareTags(expected []string, actual []string) error {
	for _, expectedTag := range expected {
		tagWasFound := false
		for _, tag := range actual {
			if expectedTag == tag {
				tagWasFound = true
				break
			}
		}
		if !tagWasFound {
			return fmt.Errorf("Expected to find tag %s, but it was not present", expectedTag)
		}
	}
	return nil
}
