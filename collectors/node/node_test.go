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
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

func TestNew(t *testing.T) {
	cfg := Collector{
		PollPeriod: 1,
	}

	c, chn := New(cfg, collectors.NodeInfo{})
	if c.PollPeriod != 1 {
		t.Error("Expected pollperiod to be 1, got", c.PollPeriod)
	}

	if len(chn) != 0 {
		t.Error("expected empty channel, got", len(chn))
	}
}

func TestTransform(t *testing.T) {
	mockers := []nodeMetricPoller{
		&mockProcess,
		&mockUptime,
		&mockCpuMetric,
		&mockLoad,
		&mockMemory,
		&mockFS,
		&mockNet,
	}
	mockNodeCollector := nodeCollector{}

	for _, m := range mockers {
		m.addDatapoints(&mockNodeCollector)
	}

	Convey("When transforming agent metrics to fit producers.MetricsMessage", t, func() {
		testTime, err := time.Parse(time.RFC3339Nano, "2009-11-10T23:00:00Z")
		if err != nil {
			panic(err)
		}

		nc := Collector{
			PollPeriod:  60,
			MetricsChan: make(chan producers.MetricsMessage),
			nodeInfo: collectors.NodeInfo{
				MesosID:   "test-mesos-id",
				ClusterID: "test-cluster-id",
			},
			nodeMetrics: mockNodeCollector.datapoints,
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
