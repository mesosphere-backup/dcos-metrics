// +build unit

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
	"net/http"
	"net/http/httptest"
	"net/url"
	"strconv"
	"strings"
	"testing"

	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

var (
	mockAgentMetrics = []byte(`
		{
			"system\/cpus_total": 2,
			"system\/load_1min": 0.1,
			"system\/load_5min": 0.11,
			"system\/load_15min": 0.08,
			"system\/mem_total_bytes": 4145348608,
			"system\/mem_free_bytes": 3349942272
		}`)

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

func TestNewAgent(t *testing.T) {
	Convey("When establishing a new agentPoller", t, func() {
		Convey("Should return an error when given an improper IP address", func() {
			_, err := NewAgent("", 10000, 60, make(chan<- producers.MetricsMessage))
			So(err, ShouldNotBeNil)
		})

		Convey("Should return an error when given an improper port", func() {
			_, err := NewAgent("1.2.3.4", 1023, 60, make(chan<- producers.MetricsMessage))
			So(err, ShouldNotBeNil)
		})

		Convey("Should return an error when given an improper pollPeriod", func() {
			_, err := NewAgent("1.2.3.4", 1024, 0, make(chan<- producers.MetricsMessage))
			So(err, ShouldNotBeNil)
		})

		Convey("Should return an Agent when given proper inputs", func() {
			a, err := NewAgent("1.2.3.4", 10000, 60, make(chan<- producers.MetricsMessage))
			So(a, ShouldHaveSameTypeAs, Agent{})
			So(err, ShouldBeNil)
		})
	})
}

func TestGetContainerMetrics(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		w.Write(mockContainerMetrics)
	}))
	defer ts.Close()

	Convey("When fetching container metrics", t, func() {
		port, err := extractPortFromURL(ts.URL)
		if err != nil {
			panic(err)
		}

		a, _ := NewAgent("/bin/true", port, 60, make(chan<- producers.MetricsMessage))
		a.AgentIP = "127.0.0.1"
		result, err := a.getContainerMetrics()

		Convey("Should return an array of 'agentContainer' without error", func() {
			// Ensure that we're
			//   a) unmarshaling the data correctly, and
			//   b) that we're getting valid types for the data (string, float, int)
			So(result[0].ContainerID, ShouldEqual, "e4faacb2-f69f-4ea1-9d96-eb06fea75eef")
			So(result[0].Statistics.CpusLimit, ShouldEqual, 1.1)
			So(result[0].Statistics.MemTotalBytes, ShouldEqual, 4476928)
			So(err, ShouldBeNil)
		})
	})
}

func TestGetAgentMetrics(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		w.Write(mockAgentMetrics)
	}))
	defer ts.Close()

	Convey("When fetching agent metrics", t, func() {
		port, err := extractPortFromURL(ts.URL)
		if err != nil {
			panic(err)
		}

		Convey("Should return an 'agentMetricsSnapshot' without error", func() {
			a, _ := NewAgent("/bin/true", port, 60, make(chan<- producers.MetricsMessage))
			a.AgentIP = "127.0.0.1"
			result, err := a.getAgentMetrics()

			So(result.CPUsTotal, ShouldEqual, 2)
			So(result.SystemLoad5Min, ShouldEqual, 0.11)
			So(result.MemFreeBytes, ShouldEqual, 3349942272)
			So(err, ShouldBeNil)
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

	Convey("When fetching the agent state", t, func() {
		port, err := extractPortFromURL(ts.URL)
		if err != nil {
			panic(err)
		}

		Convey("Should return an 'agentState' without error", func() {
			a, _ := NewAgent("/bin/true", port, 60, make(chan<- producers.MetricsMessage))
			a.AgentIP = "127.0.0.1"
			result, err := a.getAgentState()

			// getAgentState() returns a lot of metadata required for dcos-metrics
			// to be useful to operators. Let's ensure that we're unmarshaling
			// and able to return *everything* we care about.
			So(len(result.Frameworks), ShouldEqual, 1)
			So(len(result.Frameworks[0].Executors), ShouldEqual, 1)
			So(result.Frameworks[0].ID, ShouldEqual, "5349f49b-68b3-4638-aab2-fc4ec845f993-0000")
			So(result.Frameworks[0].Name, ShouldEqual, "marathon")
			So(result.Frameworks[0].Role, ShouldEqual, "*")
			So(result.Frameworks[0].Executors[0].ID, ShouldEqual, "foo.124b1048-a17a-11e6-9182-080027fb5b88")
			So(result.Frameworks[0].Executors[0].Name, ShouldEqual, "Command Executor (Task: foo.124b1048-a17a-11e6-9182-080027fb5b88) (Command: sh -c 'sleep 900')")
			So(result.Frameworks[0].Executors[0].Container, ShouldEqual, "e4c2f9f6-47aa-481d-a183-a21e8435bc06")
			So(result.Frameworks[0].Executors[0].Labels[0].Key, ShouldEqual, "somekey")
			So(result.Frameworks[0].Executors[0].Labels[0].Value, ShouldEqual, "someval")
			So(err, ShouldBeNil)
		})
	})
}

func TestGetIP(t *testing.T) {
	Convey("When getting the agent IP address using the ip_detect script", t, func() {
		Convey("Should return the IP address without error", nil)
	})
}

func TestTransform(t *testing.T) {
	Convey("When transforming agent metrics to fit producers.MetricsMessage", t, func() {
		Convey("Should return a []producers.MetricsMessage without errors", nil)
	})
}

func TestGetFrameworkNameByFrameworkID(t *testing.T) {
	Convey("When getting a framework's name, given its ID", t, func() {
		fi := []frameworkInfo{
			frameworkInfo{
				Name: "fooframework",
				ID:   "7",
			},
		}

		Convey("Should return the framework name without errors", func() {
			result := getFrameworkNameByFrameworkID("7", fi)
			So(result, ShouldEqual, "fooframework")
		})

		Convey("Should return an empty string if no match was found", func() {
			result := getFrameworkNameByFrameworkID("42", fi)
			So(result, ShouldEqual, "")
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
