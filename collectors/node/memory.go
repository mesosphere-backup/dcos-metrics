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

	mem, err := getMemory()
	if err != nil {
		return err
	}

	m.memTotal = mem.Total
	m.memFree = mem.Free
	m.memBuffers = mem.Buffers
	m.memCached = mem.Cached

	swap, err := getSwap()
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
			Name:      MEM_TOTAL,
			Unit:      BYTES,
			Value:     m.memTotal,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      MEM_FREE,
			Unit:      BYTES,
			Value:     m.memFree,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      MEM_BUFFERS,
			Unit:      BYTES,
			Value:     m.memBuffers,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      MEM_CACHED,
			Unit:      BYTES,
			Value:     m.memCached,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      SWAP_TOTAL,
			Unit:      BYTES,
			Value:     m.swapTotal,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      SWAP_FREE,
			Unit:      BYTES,
			Value:     m.swapFree,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      SWAP_USED,
			Unit:      BYTES,
			Value:     m.swapUsed,
			Timestamp: m.timestamp,
		},
	}, nil
}

/* Helpers */

func getMemory() (*mem.VirtualMemoryStat, error) {
	m, err := mem.VirtualMemory()
	return m, err
}

func getSwap() (*mem.SwapMemoryStat, error) {
	s, err := mem.SwapMemory()
	return s, err
}
