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

package node

import (
	"encoding/json"
	"fmt"
	"net/url"
	"strconv"
	"strings"
	"testing"
	"time"

	httpHelpers "github.com/dcos/dcos-metrics/http_helpers"
	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

func TestBuildDatapoints(t *testing.T) {
	testTime, err := time.Parse(time.RFC3339Nano, "2009-11-10T23:00:00Z")
	if err != nil {
		panic(err)
	}

	Convey("When building a slice of producers.Datapoint for a MetricsMessage", t, func() {
		Convey("Should return the node's datapoints with valid tags and values", func() {
			result := buildDatapoints(mockNodeMetrics, testTime)
			So(len(result), ShouldEqual, 46)
			So(result[0].Name, ShouldEqual, "uptime")
			So(result[0].Unit, ShouldEqual, "")            // TODO(roger): no easy way to get units
			So(result[0].Value, ShouldEqual, uint64(7865)) // TODO(roger): everything is a string for MVP
			So(result[0].Timestamp, ShouldEqual, "2009-11-10T23:00:00Z")
		})

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
}

func TestNewDCOSHost(t *testing.T) {
	testClient, err := httpHelpers.NewMetricsClient("", "")
	if err != nil {
		t.Error("Error retreiving HTTP Client:", err)
	}

	Convey("When establishing a new DCOSHost object", t, func() {
		Convey("Should return an error when given an improper port", func() {
			_, err := NewDCOSHost(
				"agent",
				"10.0.0.1",
				"test-mesos-id",
				"test_cluster-id",
				1023,
				60,
				testClient,
				make(chan<- producers.MetricsMessage))
			So(err, ShouldNotBeNil)
		})

		Convey("Should return an error when given an improper pollPeriod", func() {
			_, err := NewDCOSHost(
				"agent",
				"10.0.0.1",
				"test-mesos-id",
				"test_cluster-id",
				1024,
				0,
				testClient,
				make(chan<- producers.MetricsMessage))
			So(err, ShouldNotBeNil)
		})

		Convey("Should return an Agent when given proper inputs", func() {
			a, err := NewDCOSHost(
				"agent",
				"10.0.0.1",
				"test-mesos-id",
				"test_cluster-id",
				10000,
				60,
				testClient,
				make(chan<- producers.MetricsMessage))
			So(a, ShouldHaveSameTypeAs, DCOSHost{})
			So(err, ShouldBeNil)
		})
	})
}

func TestTransform(t *testing.T) {
	testClient, err := httpHelpers.NewMetricsClient("", "")
	if err != nil {
		t.Error("Error retreiving HTTP Client:", err)
	}

	Convey("When transforming agent metrics to fit producers.MetricsMessage", t, func() {
		// bogus port and IP address here; no HTTP client in a.transform()
		h, _ := NewDCOSHost(
			"agent",
			"10.0.0.1",
			"test-mesos-id",
			"test_cluster-id",
			9000,
			60,
			testClient,
			make(chan<- producers.MetricsMessage))

		// The mocks in this test file are bytearrays so that they can be used
		// by the HTTP test server(s). So we need to unmarshal them here before
		// they can be used by a.transform().
		var thisAgentState agentState
		var thisContainerMetrics []agentContainer
		if err := json.Unmarshal(mockAgentState, &thisAgentState); err != nil {
			panic(err)
		}
		if err := json.Unmarshal(mockContainerMetrics, &thisContainerMetrics); err != nil {
			panic(err)
		}

		testTime, err := time.Parse(time.RFC3339Nano, "2009-11-10T23:00:00Z")
		if err != nil {
			panic(err)
		}
		testData := metricsMeta{
			nodeMetrics:      mockNodeMetrics,
			agentState:       thisAgentState,
			containerMetrics: thisContainerMetrics,
			timestamp:        testTime.UTC().Unix(),
		}

		Convey("Should return a []producers.MetricsMessage without errors", func() {
			result := h.transform(testData)
			So(len(result), ShouldEqual, 2) // one agent message, one container message

			// From the implementation of a.transform() and the mocks in this test file,
			// result[0] will be agent metrics, and result[1] will be container metrics.
			So(result[0].Datapoints[0].Timestamp, ShouldEqual, "2009-11-10T23:00:00Z")
			So(result[1].Datapoints[0].Timestamp, ShouldEqual, "2009-11-10T23:00:00Z")
			So(result[1].Dimensions.FrameworkName, ShouldEqual, "marathon")
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
