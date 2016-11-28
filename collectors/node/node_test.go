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
	"net/url"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/collectors"
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
			So(result[0].Unit, ShouldEqual, "") // TODO(roger): no easy way to get units
			So(result[0].Value, ShouldEqual, uint64(7865))
			So(result[0].Timestamp, ShouldEqual, "2009-11-10T23:00:00Z")
		})
	})
}

func TestTransform(t *testing.T) {
	Convey("When transforming agent metrics to fit producers.MetricsMessage", t, func() {
		testTime, err := time.Parse(time.RFC3339Nano, "2009-11-10T23:00:00Z")
		if err != nil {
			panic(err)
		}

		nc := Collector{
			PollPeriod:  60,
			MetricsChan: make(chan producers.MetricsMessage),
			NodeInfo: collectors.NodeInfo{
				MesosID:   "test-mesos-id",
				ClusterID: "test-cluster-id",
			},
			nodeMetrics: mockNodeMetrics,
			timestamp:   testTime.UTC().Unix(),
		}

		Convey("Should return a []producers.MetricsMessage without errors", func() {
			result := nc.transform()
			So(len(result), ShouldEqual, 1) // one node message

			// From the implementation of a.transform() and the mocks in this test file,
			// result[0] will be container metrics.
			So(result[0].Datapoints[0].Timestamp, ShouldEqual, "2009-11-10T23:00:00Z")
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
