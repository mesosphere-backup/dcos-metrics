//+build unit

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

package agent

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
	httpHelpers "github.com/dcos/dcos-metrics/util/http/helpers"
	. "github.com/smartystreets/goconvey/convey"
)

var (
	mockAgentState = []byte(`
		{
			"frameworks": [
				{
					"id": "5349f49b-68b3-4638-aab2-fc4ec845f993-0000",
					"name": "marathon",
					"role": "*",
					"executors": [
						{
							"id": "foo.124b1048-a17a-11e6-9182-080027fb5b88",
							"name": "Command Executor (Task: foo.124b1048-a17a-11e6-9182-080027fb5b88) (Command: sh -c 'sleep 900')",
							"container": "e4c2f9f6-47aa-481d-a183-a21e8435bc06",
							"labels": [
								{
									"key": "somekey",
									"value": "someval"
								}
							],
							"tasks": [
								{
									"id": "foo.124b1048-a17a-11e6-9182-080027fb5b88",
									"name": "foo",
									"framework_id": "5349f49b-68b3-4638-aab2-fc4ec845f993-0000",
									"executor_id": "",
									"slave_id": "34b46033-69c0-4663-887c-f64b526e47a6-S0",
									"labels": [
										{
											"key": "somekey",
											"value": "someval"
										}
									]
								}
							]
						}
					]
				}
			]
		}`)

	mockContainerMetrics = []byte(`
		[
			{
				"container_id": "e4faacb2-f69f-4ea1-9d96-eb06fea75eef",
				"executor_id": "foo.adf2b6f4-a171-11e6-9182-080027fb5b88",
				"executor_name": "Command Executor (Task: foo.adf2b6f4-a171-11e6-9182-080027fb5b88) (Command: sh -c 'sleep 900')",
				"framework_id": "5349f49b-68b3-4638-aab2-fc4ec845f993-0000",
				"source": "foo.adf2b6f4-a171-11e6-9182-080027fb5b88",
				"statistics": {
					"cpus_limit": 1.1,
					"cpus_system_time_secs": 0.31,
					"cpus_user_time_secs": 0.22,
					"mem_limit_bytes": 167772160,
					"mem_total_bytes": 4476928
				}
			}
		]`)
)

func TestGetContainerMetrics(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		w.Write(mockContainerMetrics)
	}))
	defer ts.Close()

	testClient, err := httpHelpers.NewMetricsClient("", "")
	if err != nil {
		t.Error("Error retreiving HTTP Client:", err)
	}

	port, err := extractPortFromURL(ts.URL)
	if err != nil {
		panic(err)
	}

	mac := Collector{
		Port:        port,
		PollPeriod:  60,
		MetricsChan: make(chan producers.MetricsMessage),
		NodeInfo: collectors.NodeInfo{
			IPAddress: "127.0.0.1",
			MesosID:   "test-mesos-id",
			ClusterID: "test-cluster-id",
		},
		HTTPClient:      testClient,
		RequestProtocol: "http",
	}

	Convey("When fetching container metrics", t, func() {

		err := mac.getContainerMetrics()

		Convey("Should return an array of 'agentContainer' without error", func() {
			// Ensure that we're
			//   a) unmarshaling the data correctly, and
			//   b) that we're getting valid types for the data (string, float, int)

			So(err, ShouldBeNil)
			So(mac.containerMetrics[0].ContainerID, ShouldEqual, "e4faacb2-f69f-4ea1-9d96-eb06fea75eef")
			So(mac.containerMetrics[0].Statistics.CpusLimit, ShouldEqual, 1.1)
			So(mac.containerMetrics[0].Statistics.MemTotalBytes, ShouldEqual, 4476928)
		})
	})
}

func TestGetAgentState(t *testing.T) {

	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		w.Write(mockAgentState)
	}))
	defer ts.Close()

	testClient, err := httpHelpers.NewMetricsClient("", "")
	if err != nil {
		t.Error("Error retreiving HTTP Client:", err)
	}

	port, err := extractPortFromURL(ts.URL)
	if err != nil {
		panic(err)
	}

	mac := Collector{
		Port:        port,
		PollPeriod:  60,
		MetricsChan: make(chan producers.MetricsMessage),
		NodeInfo: collectors.NodeInfo{
			IPAddress: "127.0.0.1",
			MesosID:   "test-mesos-id",
			ClusterID: "test-cluster-id",
		},
		HTTPClient:      testClient,
		RequestProtocol: "http",
	}

	Convey("When fetching the agent state", t, func() {
		Convey("Should return an 'agentState' without error", func() {
			err := mac.getAgentState()

			// getAgentState() returns a lot of metadata required for dcos-metrics
			// to be useful to operators. Let's ensure that we're unmarshaling
			// and able to return *everything* we care about.
			So(len(mac.agentState.Frameworks), ShouldEqual, 1)
			So(len(mac.agentState.Frameworks[0].Executors), ShouldEqual, 1)
			So(mac.agentState.Frameworks[0].ID, ShouldEqual, "5349f49b-68b3-4638-aab2-fc4ec845f993-0000")
			So(mac.agentState.Frameworks[0].Name, ShouldEqual, "marathon")
			So(mac.agentState.Frameworks[0].Role, ShouldEqual, "*")
			So(mac.agentState.Frameworks[0].Executors[0].ID, ShouldEqual, "foo.124b1048-a17a-11e6-9182-080027fb5b88")
			So(mac.agentState.Frameworks[0].Executors[0].Name, ShouldEqual, "Command Executor (Task: foo.124b1048-a17a-11e6-9182-080027fb5b88) (Command: sh -c 'sleep 900')")
			So(mac.agentState.Frameworks[0].Executors[0].Container, ShouldEqual, "e4c2f9f6-47aa-481d-a183-a21e8435bc06")
			So(mac.agentState.Frameworks[0].Executors[0].Labels[0].Key, ShouldEqual, "somekey")
			So(mac.agentState.Frameworks[0].Executors[0].Labels[0].Value, ShouldEqual, "someval")
			So(err, ShouldBeNil)
		})
	})
}

func TestBuildDatapoints(t *testing.T) {
	testTime, err := time.Parse(time.RFC3339Nano, "2009-11-10T23:00:00Z")
	if err != nil {
		panic(err)
	}

	Convey("When building a slice of producers.Datapoint for a MetricsMessage", t, func() {
		Convey("Should return the node's datapoints with valid tags and values", func() {
			Convey("Should return a container's datapoints with valid tags and values", func() {
				var thisContainerMetrics []agentContainer
				if err := json.Unmarshal(mockContainerMetrics, &thisContainerMetrics); err != nil {
					panic(err)
				}

				result := []producers.Datapoint{}
				for _, c := range thisContainerMetrics {
					pts := buildDatapoints(c, testTime)
					result = append(result, pts...)
				}
				for _, r := range result {
					fmt.Println(r.Name)
				}
				So(len(result), ShouldEqual, 16)
				So(result[0].Timestamp, ShouldEqual, "2009-11-10T23:00:00Z")
			})
		})
	})
}

func TestTransform(t *testing.T) {
	Convey("When transforming agent metrics to fit producers.MetricsMessage", t, func() {
		mac := Collector{
			PollPeriod:  60,
			MetricsChan: make(chan producers.MetricsMessage),
			NodeInfo: collectors.NodeInfo{
				MesosID:   "test-mesos-id",
				ClusterID: "test-cluster-id",
			},
		}

		// The mocks in this test file are bytearrays so that they can be used
		// by the HTTP test server(s). So we need to unmarshal them here before
		// they can be used by a.transform().
		if err := json.Unmarshal(mockAgentState, &mac.agentState); err != nil {
			panic(err)
		}
		if err := json.Unmarshal(mockContainerMetrics, &mac.containerMetrics); err != nil {
			panic(err)
		}

		Convey("Should return a []producers.MetricsMessage without errors", func() {
			result := mac.transformContainerMetrics()
			So(len(result), ShouldEqual, 1) // one container message

			// From the implementation of a.transform() and the mocks in this test file,
			// result[0] will be agent metrics, and result[1] will be container metrics.
			So(result[0].Dimensions.FrameworkName, ShouldEqual, "marathon")
		})
	})
}

func TestGetFrameworkInfoByFrameworkID(t *testing.T) {
	Convey("When getting a framework's info, given its ID", t, func() {
		fi := []frameworkInfo{
			frameworkInfo{
				Name:      "fooframework",
				ID:        "7",
				Role:      "foorole",
				Principal: "fooprincipal",
			},
		}

		Convey("Should return the framework name without errors", func() {
			result, ok := getFrameworkInfoByFrameworkID("7", fi)
			So(ok, ShouldBeTrue)
			So(result.Name, ShouldEqual, "fooframework")
			So(result.ID, ShouldEqual, "7")
			So(result.Role, ShouldEqual, "foorole")
			So(result.Principal, ShouldEqual, "fooprincipal")
		})

		Convey("Should return an empty frameworkInfo if no match was found", func() {
			result, ok := getFrameworkInfoByFrameworkID("42", fi)
			So(result, ShouldResemble, frameworkInfo{})
			So(ok, ShouldBeFalse)
		})
	})
}

func TestGetLabelsByContainerID(t *testing.T) {
	Convey("When getting the labels for a container, given its ID", t, func() {
		fi := []frameworkInfo{
			frameworkInfo{
				Name: "fooframework",
				ID:   "7",
				Executors: []executorInfo{
					executorInfo{
						Container: "someContainerID",
						Labels: []executorLabels{
							executorLabels{
								Key:   "somekey",
								Value: "someval",
							},
						},
					},
				},
			},
		}

		Convey("Should return a map of key/value pairs", func() {
			result := getLabelsByContainerID("someContainerID", fi)
			So(result, ShouldResemble, map[string]string{"somekey": "someval"})
		})

		Convey("Should return an empty map if no labels were present", func() {
			result := getLabelsByContainerID("someOtherContainerID", fi)
			So(result, ShouldResemble, map[string]string{})
		})
	})
}

func extractPortFromURL(u string) (int, error) {
	parsed, err := url.Parse(u)
	if err != nil {
		return 0, err
	}
	port, err := strconv.Atoi(strings.Split(parsed.Host, ":")[1])
	if err != nil {
		return 0, err
	}
	return port, nil
}
