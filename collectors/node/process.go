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

type processMetrics struct {
	processCount uint64
	timestamp    string
}

func (m *processMetrics) poll() error {
	procs, err := getProcessCount()
	if err != nil {
		return err
	}

	m.processCount = procs
	m.timestamp = thisTime()
	return nil
}

func (m *processMetrics) getDatapoints() ([]producers.Datapoint, error) {
	return []producers.Datapoint{
		producers.Datapoint{
			Name:      processCount,
			Unit:      countUnit,
			Value:     m.processCount,
			Timestamp: m.timestamp,
		}}, nil
}

func getProcessCount() (uint64, error) {
	i, err := host.Info()
	if err != nil {
		return 0, err
	}

	return i.Procs, nil
}
