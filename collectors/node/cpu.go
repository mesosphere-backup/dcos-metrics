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
	"math"
	"sync"

	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/cpu"
)

type cpuCoresMetric struct {
	cpuCores  int32
	cpuTotal  float64
	cpuUser   float64
	cpuSystem float64
	cpuIdle   float64
	cpuWait   float64
	timestamp string
}

func (m *cpuCoresMetric) poll() error {
	ts := thisTime()
	hi, err := getNumCores()
	if err != nil {
		return err
	}

	currentTime, lastTime, err := getCPUTimes()
	if err != nil {
		return err
	}

	cpuStatePcts := calculatePcts(currentTime, lastTime)

	m.cpuCores = hi
	m.timestamp = ts
	m.cpuTotal = cpuStatePcts.User + cpuStatePcts.System
	m.cpuUser = cpuStatePcts.User
	m.cpuSystem = cpuStatePcts.System
	m.cpuIdle = cpuStatePcts.Idle
	m.cpuWait = cpuStatePcts.Iowait

	return nil
}

func (m *cpuCoresMetric) getDatapoints() ([]producers.Datapoint, error) {
	return []producers.Datapoint{
		producers.Datapoint{
			Name:      CPU_CORES,
			Unit:      COUNT,
			Value:     m.cpuCores,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_TOTAL,
			Unit:      PERCENT,
			Value:     m.cpuTotal,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_USER,
			Unit:      PERCENT,
			Value:     m.cpuUser,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_SYSTEM,
			Unit:      PERCENT,
			Value:     m.cpuSystem,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_IDLE,
			Unit:      PERCENT,
			Value:     m.cpuIdle,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_WAIT,
			Unit:      PERCENT,
			Value:     m.cpuWait,
			Timestamp: m.timestamp,
		},
	}, nil
}

// -- helpers
func getNumCores() (int32, error) {
	cores := int32(0)
	cpus, err := cpu.Info()
	if err != nil {
		return cores, err
	}
	for _, c := range cpus {
		cores += c.Cores
	}
	return cores, nil
}

/* CPU Helper Methods */
type lastCPUTimes struct {
	sync.Mutex
	times cpu.TimesStat
}

var lastCPU lastCPUTimes

func init() {
	t, _ := cpu.Times(false) // get totals, not per-cpu stats
	lastCPU.Lock()
	lastCPU.times = t[0]
	lastCPU.Unlock()
}

func getCPUTimes() (cpu.TimesStat, cpu.TimesStat, error) {
	currentTimes, err := cpu.Times(false) // get totals, not per-cpu stats
	if err != nil {
		return cpu.TimesStat{}, cpu.TimesStat{}, err
	}

	lastTimes := lastCPU.times

	lastCPU.Lock()
	lastCPU.times = currentTimes[0] // update lastTimes to the currentTimes
	lastCPU.Unlock()

	return currentTimes[0], lastTimes, nil
}

// calculatePct returns the percent utilization for CPU states. 100.00 => 100.00%
func calculatePcts(lastTimes cpu.TimesStat, curTimes cpu.TimesStat) cpu.TimesStat {
	totalDelta := curTimes.Total() - lastTimes.Total()
	if totalDelta == 0 {
		totalDelta = 1 // can't divide by zero
	}
	return cpu.TimesStat{
		User:      formatPct(curTimes.User-lastTimes.User, totalDelta),
		System:    formatPct(curTimes.System-lastTimes.System, totalDelta),
		Idle:      formatPct(curTimes.Idle-lastTimes.Idle, totalDelta),
		Nice:      formatPct(curTimes.Nice-lastTimes.Nice, totalDelta),
		Iowait:    formatPct(curTimes.Iowait-lastTimes.Iowait, totalDelta),
		Irq:       formatPct(curTimes.Irq-lastTimes.Irq, totalDelta),
		Softirq:   formatPct(curTimes.Softirq-lastTimes.Softirq, totalDelta),
		Steal:     formatPct(curTimes.Steal-lastTimes.Steal, totalDelta),
		Guest:     formatPct(curTimes.Guest-lastTimes.Guest, totalDelta),
		GuestNice: formatPct(curTimes.GuestNice-lastTimes.GuestNice, totalDelta),
		Stolen:    formatPct(curTimes.Stolen-lastTimes.Stolen, totalDelta),
	}
}

// Helper function: derives percentage, rounds to 2dp and sanitises
func formatPct(f float64, div float64) float64 {
	rounded := math.Floor((f / div * 10000) + .5)
	return math.Max(rounded/100, 0)
}
