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
	"github.com/shirou/gopsutil/host"
)

type uptimeMetric struct {
	uptime    uint64
	timestamp string
}

func (m *uptimeMetric) poll() error {
	uptime, err := host.Uptime()
	if err != nil {
		return err
	}
	m.uptime = uptime
	m.timestamp = thisTime()
	return nil
}

func (m *uptimeMetric) addDatapoints(nc *nodeCollector) error {
	nc.datapoints = append(nc.datapoints, producers.Datapoint{
		Name:      UPTIME,
		Unit:      COUNT,
		Value:     m.uptime,
		Timestamp: m.timestamp,
	})
	return nil
}
