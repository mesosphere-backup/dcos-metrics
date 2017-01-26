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
	"time"

	"github.com/dcos/dcos-metrics/producers"
)

const (
	// Unit constants
	COUNT = "count"
	BYTES = "bytes"
	PID   = "pid"
)

type nodeCollector struct {
	datapoints []producers.Datapoint
}

type nodeMetricPoller interface {
	poll() error
	getDatapoints() ([]producers.Datapoint, error)
}

func getNodeMetrics() ([]producers.Datapoint, error) {
	nc := nodeCollector{}

	nodeMetricPollers := []nodeMetricPoller{
		&uptimeMetric{},
		&cpuCoresMetric{},
		&loadMetric{},
		&filesystemMetrics{},
		&memoryMetric{},
		&networkMetrics{},
		&processMetrics{},
	}

	// For each metric poller defined, execute .poll() to get the latest
	// metric, check for errors, and iterate over the slice of producers.Datapoint
	// returned by .poll(), adding them to our top scope nodeMetrics slice.
	for _, mp := range nodeMetricPollers {
		if err := mp.poll(); err != nil {
			return nc.datapoints, err
		}

		if dps, err := mp.getDatapoints(); err != nil {
			return nc.datapoints, err
		} else {
			nc.datapoints = append(nc.datapoints, dps...)
		}
	}

	return nc.datapoints, nil
}

// -- helpers

func thisTime() string {
	return time.Now().UTC().Format(time.RFC3339Nano)
}
