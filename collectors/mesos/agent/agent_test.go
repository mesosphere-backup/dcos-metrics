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
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strconv"
	"strings"
	"testing"

	"github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
	httpHelpers "github.com/dcos/dcos-metrics/util/http/helpers"
	. "github.com/smartystreets/goconvey/convey"
)

var (
	mockAgentState = loadFixture("agent-state.json")
	// For now, mockMasterState only includes framework infos and flags related to framework auth.
	mockMasterState               = loadFixture("master-state.json")
	mockContainerMetrics          = loadFixture("container-metrics.json")
	mockContainerMetricsNoStats   = loadFixture("container-metrics-no-statistics.json")
	mockContainerMetricsWithBlkio = loadFixture("container-metrics-blkio.json")
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
		Port:            port,
		PollPeriod:      60,
		HTTPClient:      testClient,
		RequestProtocol: "http",
		metricsChan:     make(chan producers.MetricsMessage),
		nodeInfo: collectors.NodeInfo{
			IPAddress: "127.0.0.1",
			MesosID:   "test-mesos-id",
			ClusterID: "test-cluster-id",
		},
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
		Port:            port,
		PollPeriod:      60,
		HTTPClient:      testClient,
		RequestProtocol: "http",
		metricsChan:     make(chan producers.MetricsMessage),
		nodeInfo: collectors.NodeInfo{
			IPAddress: "127.0.0.1",
			MesosID:   "test-mesos-id",
			ClusterID: "test-cluster-id",
		},
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

func TestUpdateContainerRels(t *testing.T) {
	var as agentState
	if err := json.Unmarshal(mockAgentState, &as); err != nil {
		panic(err)
	}

	Convey("Before the container relationships have been built", t, func() {
		ctr := NewContainerTaskRels()
		Convey("Attempting to get any task should yield nil", func() {
			t := ctr.Get("e4faacb2-f69f-4ea1-9d96-eb06fea75eef")
			So(t, ShouldBeNil)
		})
	})

	Convey("After building container relationships from the updated agent state", t, func() {
		ctr := NewContainerTaskRels()
		ctr.update(as)
		Convey("Each task should be available by container ID", func() {
			So(len(ctr.rels), ShouldEqual, 1)
			t := ctr.Get("e4faacb2-f69f-4ea1-9d96-eb06fea75eef")
			So(t.ID, ShouldEqual, "foo.124b1048-a17a-11e6-9182-080027fb5b88")
			So(t.Name, ShouldEqual, "foo")
		})

		Convey("Attempting to get a missing task should yield nil", func() {
			t := ctr.Get("this-is-not-a-container-ID")
			So(t, ShouldBeNil)
		})
	})
}

func TestBuildDatapoints(t *testing.T) {

	checkCIDRegistry := func(registry []string) {
		if len(registry) <= 1 {
			return
		}
		if registry[len(registry)-1] != registry[len(registry)-2] {
			t.Errorf("all container ID's for a datapoint set must be the same, got %+v", registry)
		}
	}

	Convey("When building a slice of producers.Datapoint for a MetricsMessage", t, func() {
		Convey("Should return the node's datapoints with valid tags and values", func() {
			Convey("Should return a container's datapoints with valid tags and values", func() {
				var thisContainerMetrics []agentContainer
				if err := json.Unmarshal(mockContainerMetrics, &thisContainerMetrics); err != nil {
					panic(err)
				}

				coll := Collector{
					log:              logrus.WithFields(logrus.Fields{"test": "datapoints"}),
					containerMetrics: thisContainerMetrics,
				}

				for _, container := range thisContainerMetrics {
					result, err := coll.createContainerDatapoints(container)
					So(err, ShouldEqual, nil)
					So(len(result), ShouldEqual, 30)

					cidRegistry := []string{}
					for _, dp := range result {
						So(len(dp.Tags), ShouldEqual, 5)
						So(dp.Tags, ShouldContainKey, "container_id")
						So(dp.Tags, ShouldContainKey, "source")
						So(dp.Tags, ShouldContainKey, "framework_id")
						So(dp.Tags, ShouldContainKey, "executor_id")
						So(dp.Tags, ShouldContainKey, "executor_name")

						So(len(dp.Tags["container_id"]), ShouldBeGreaterThan, 0)
						So(len(dp.Tags["source"]), ShouldBeGreaterThan, 0)
						So(len(dp.Tags["framework_id"]), ShouldBeGreaterThan, 0)
						So(len(dp.Tags["executor_id"]), ShouldBeGreaterThan, 0)
						So(len(dp.Tags["executor_name"]), ShouldBeGreaterThan, 0)

						cidRegistry = append(cidRegistry, dp.Tags["container_id"])
						checkCIDRegistry(cidRegistry)
					}
				}
			})
		})
	})
}

func TestTransform(t *testing.T) {
	Convey("When transforming agent metrics to fit producers.MetricsMessage", t, func() {
		mac := Collector{
			PollPeriod:  60,
			log:         logrus.WithFields(logrus.Fields{"test": "this"}),
			metricsChan: make(chan producers.MetricsMessage),
			nodeInfo: collectors.NodeInfo{
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
		Convey("With container metrics", func() {
			if err := json.Unmarshal(mockContainerMetrics, &mac.containerMetrics); err != nil {
				panic(err)
			}
			result := mac.metricsMessages()
			So(len(result), ShouldEqual, 1) // one container message

			Convey("Should return a []producers.MetricsMessage without errors", func() {
				So(result[0].Dimensions.FrameworkName, ShouldEqual, "marathon")
				So(result[0].Dimensions.FrameworkPrincipal, ShouldEqual, "dcos_marathon")
			})

			Convey("Should return task ID and name with container metrics", func() {
				So(result[0].Dimensions.TaskID, ShouldEqual, "foo.124b1048-a17a-11e6-9182-080027fb5b88")
				So(result[0].Dimensions.TaskName, ShouldEqual, "foo")
			})
		})

		Convey("Missing container metrics", func() {
			if err := json.Unmarshal(mockContainerMetricsNoStats, &mac.containerMetrics); err != nil {
				panic(err)
			}
			result := mac.metricsMessages()
			So(len(result), ShouldEqual, 1) // one container message
		})

		Convey("With blkio statistics", func() {
			if err := json.Unmarshal(mockContainerMetricsWithBlkio, &mac.containerMetrics); err != nil {
				panic(err)
			}
			result := mac.metricsMessages()
			So(len(result), ShouldEqual, 1) // one container message

			// expected stats where [bklio_device="total"]
			expected_total_stats := map[string]uint64{
				"blkio.cfq.io_merged.total":        123451,
				"blkio.cfq.io_queued.total":        123452,
				"blkio.cfq.io_service_bytes.total": 123453,
				"blkio.cfq.io_service_time.total":  123454,
				"blkio.cfq.io_serviced.total":      123455,
				"blkio.cfq.io_wait_time.total":     123456,

				"blkio.cfq_recursive.io_merged.total":        543211,
				"blkio.cfq_recursive.io_queued.total":        543212,
				"blkio.cfq_recursive.io_service_bytes.total": 543213,
				"blkio.cfq_recursive.io_service_time.total":  543214,
				"blkio.cfq_recursive.io_serviced.total":      543215,
				"blkio.cfq_recursive.io_wait_time.total":     543216,

				"blkio.throttling.io_service_bytes.total": 1234567890,
				"blkio.throttling.io_serviced.total":      9876543210,
			}

			// expected stats where [bklio_device="8:0"]
			expected_dev_stats := map[string]uint64{
				"blkio.throttling.io_service_bytes.read":  567891,
				"blkio.throttling.io_service_bytes.write": 567892,
				"blkio.throttling.io_service_bytes.sync":  567893,
				"blkio.throttling.io_service_bytes.async": 567894,
				"blkio.throttling.io_service_bytes.total": 567895,

				"blkio.throttling.io_serviced.read":  987651,
				"blkio.throttling.io_serviced.write": 987652,
				"blkio.throttling.io_serviced.sync":  987653,
				"blkio.throttling.io_serviced.async": 987654,
				"blkio.throttling.io_serviced.total": 987655,
			}

			// Build map of device : names
			actual_total_stats := map[string]uint64{}
			actual_dev_stats := map[string]uint64{}
			for _, d := range result[0].Datapoints {
				v, _ := d.Value.(uint64)
				if d.Tags["blkio_device"] == "total" {
					actual_total_stats[d.Name] = v
					continue
				}
				if d.Tags["blkio_device"] == "8:0" {
					actual_dev_stats[d.Name] = v
					continue
				}
			}

			for name, expected := range expected_total_stats {
				// Check that the stat is present
				So(actual_total_stats, ShouldContainKey, name)
				// Check that the value is correct
				So(actual_total_stats[name], ShouldEqual, expected)
			}

			for name, expected := range expected_dev_stats {
				// Check that the stat is present
				So(actual_dev_stats, ShouldContainKey, name)
				// Check that the value is correct
				So(actual_dev_stats[name], ShouldEqual, expected)
			}

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
			result := getFrameworkInfoByFrameworkID("7", fi)
			So(result, ShouldNotBeNil)
			So(result.Name, ShouldEqual, "fooframework")
			So(result.ID, ShouldEqual, "7")
			So(result.Role, ShouldEqual, "foorole")
			So(result.Principal, ShouldEqual, "fooprincipal")
		})

		Convey("Should return nil if no match was found", func() {
			result := getFrameworkInfoByFrameworkID("42", fi)
			So(result, ShouldBeNil)
		})
	})
}

func TestGetExecutorInfoByExecutorID(t *testing.T) {
	Convey("When getting an executor's info, given its ID", t, func() {
		ei := []executorInfo{
			executorInfo{
				Name:      "pierrepoint",
				ID:        "pierrepoint.1234",
				Container: "foo.container.1234556",
			},
		}
		Convey("Should return the executor's info, given an executor ID", func() {
			result := getExecutorInfoByExecutorID("pierrepoint.1234", ei)
			So(result, ShouldNotBeNil)
			So(result.ID, ShouldEqual, "pierrepoint.1234")
			So(result.Name, ShouldEqual, "pierrepoint")
		})
		Convey("Should return an empty executorInfo if no match was found", func() {
			result := getExecutorInfoByExecutorID("not-an-executor-id", ei)
			So(result, ShouldBeNil)
		})
	})
}

func TestGetTaskInfoByContainerID(t *testing.T) {
	Convey("When getting a task's info, given a container ID", t, func() {
		ti := []TaskInfo{
			TaskInfo{
				Name:     "should-not-error",
				ID:       "should-not-error.123",
				Statuses: []taskStatusInfo{},
			},
			TaskInfo{
				Name: "foo",
				ID:   "foo.123",
				Statuses: []taskStatusInfo{
					taskStatusInfo{
						ContainerStatusInfo: containerStatusInfo{
							ID: containerStatusID{
								Value: "e4faacb2-f69f-4ea1-9d96-eb06fea75eef",
							},
						},
					},
				},
			},
		}
		Convey("Should return the relevant task info without errors", func() {
			result := getTaskInfoByContainerID("e4faacb2-f69f-4ea1-9d96-eb06fea75eef", ti)
			So(result, ShouldNotBeNil)
			So(result.Name, ShouldEqual, "foo")
			So(result.ID, ShouldEqual, "foo.123")
		})
		Convey("Should return an empty frameworkInfo if no match was found", func() {
			result := getTaskInfoByContainerID("not-a-real-container-id", ti)
			So(result, ShouldBeNil)
		})
	})
}

func TestGetLabelsByContainerID(t *testing.T) {
	tl := logrus.WithFields(logrus.Fields{"test": "this"})
	Convey("When getting the labels for a container, given its ID", t, func() {
		fi := []frameworkInfo{
			frameworkInfo{
				Name: "fooframework",
				ID:   "7",
				Executors: []executorInfo{
					executorInfo{
						Container: "someContainerID",
						Labels: []KeyValue{
							KeyValue{
								Key:   "somekey",
								Value: "someval",
							},
						},
					},
					executorInfo{
						Container: "containerWithLongLabelID",
						Labels: []KeyValue{
							KeyValue{
								Key:   "somekey",
								Value: "someval",
							},
							KeyValue{
								Key:   "longkey",
								Value: "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789",
							},
							KeyValue{
								Key:   "DCOS_PACKAGE_DEFINITION",
								Value: "shortVal",
							},
						},
					},
				},
			},
		}

		Convey("Should return a map of key/value pairs", func() {
			result := getLabelsByContainerID("someContainerID", fi, tl)
			So(result, ShouldResemble, map[string]string{"somekey": "someval"})
		})

		Convey("Should return an empty map if no labels were present", func() {
			result := getLabelsByContainerID("someOtherContainerID", fi, tl)
			So(result, ShouldResemble, map[string]string{})
		})

		Convey("Should drop labels with overly long values", func() {
			result := getLabelsByContainerID("containerWithLongLabelID", fi, tl)
			So(result, ShouldResemble, map[string]string{"somekey": "someval"})
		})

		Convey("Should drop cosmos-related labels", func() {
			result := getLabelsByContainerID("containerWithLongLabelID", fi, tl)
			So(result, ShouldResemble, map[string]string{"somekey": "someval"})
		})
	})
}

func loadFixture(name string) []byte {
	// the forward-slash works correctly on Windows
	contents, err := ioutil.ReadFile("testdata/" + name)
	if err != nil {
		panic(err)
	}
	return contents
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
