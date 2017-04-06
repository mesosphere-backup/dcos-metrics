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
	"github.com/shirou/gopsutil/mem"
)

type memoryMetric struct {
	memTotal   uint64
	memFree    uint64
	memBuffers uint64
	memCached  uint64
	swapTotal  uint64
	swapFree   uint64
	swapUsed   uint64
	timestamp  string
}

func (m *memoryMetric) poll() error {
	ts := thisTime()
	m.timestamp = ts

	virt, err := mem.VirtualMemory()
	if err != nil {
		return err
	}

	m.memTotal = virt.Total
	m.memFree = virt.Free
	m.memBuffers = virt.Buffers
	m.memCached = virt.Cached

	swap, err := mem.SwapMemory()
	if err != nil {
		return err
	}

	m.swapTotal = swap.Total
	m.swapFree = swap.Free
	m.swapUsed = swap.Used

	return nil
}

func (m *memoryMetric) getDatapoints() ([]producers.Datapoint, error) {
	return []producers.Datapoint{
		producers.Datapoint{
			Name:      memTotal,
			Unit:      bytesUnit,
			Value:     m.memTotal,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      memFree,
			Unit:      bytesUnit,
			Value:     m.memFree,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      memBuffers,
			Unit:      bytesUnit,
			Value:     m.memBuffers,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      memCached,
			Unit:      bytesUnit,
			Value:     m.memCached,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      swapTotal,
			Unit:      bytesUnit,
			Value:     m.swapTotal,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      swapFree,
			Unit:      bytesUnit,
			Value:     m.swapFree,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      swapUsed,
			Unit:      bytesUnit,
			Value:     m.swapUsed,
			Timestamp: m.timestamp,
		},
	}, nil
}
