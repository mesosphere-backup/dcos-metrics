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
	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/load"
)

type loadMetric struct {
	load1Min  float64
	load5Min  float64
	load15Min float64
	timestamp string
}

func (m *loadMetric) poll() error {
	ts := thisTime()
	l, err := load.Avg()
	if err != nil {
		return err
	}

	m.load1Min = l.Load1
	m.load5Min = l.Load5
	m.load15Min = l.Load15
	m.timestamp = ts

	return nil
}

func (m *loadMetric) addDatapoints(nc *nodeCollector) error {
	loadDps := []producers.Datapoint{
		producers.Datapoint{
			Name:      LOAD_1MIN,
			Unit:      COUNT,
			Value:     m.load1Min,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      LOAD_5MIN,
			Unit:      COUNT,
			Value:     m.load5Min,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      LOAD_15MIN,
			Unit:      COUNT,
			Value:     m.load15Min,
			Timestamp: m.timestamp,
		},
	}
	for _, dp := range loadDps {
		nc.datapoints = append(nc.datapoints, dp)
	}

	return nil
}
