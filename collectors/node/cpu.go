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
			Unit:      COUNT,
			Value:     m.cpuTotal,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_USER,
			Unit:      COUNT,
			Value:     m.cpuUser,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_SYSTEM,
			Unit:      COUNT,
			Value:     m.cpuSystem,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_IDLE,
			Unit:      COUNT,
			Value:     m.cpuIdle,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      CPU_WAIT,
			Unit:      COUNT,
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
		User:      gtZero(round((curTimes.User - lastTimes.User) / totalDelta * 100)),
		System:    gtZero(round((curTimes.System - lastTimes.System) / totalDelta * 100)),
		Idle:      gtZero(round((curTimes.Idle - lastTimes.Idle) / totalDelta * 100)),
		Nice:      gtZero(round((curTimes.Nice - lastTimes.Nice) / totalDelta * 100)),
		Iowait:    gtZero(round((curTimes.Iowait - lastTimes.Iowait) / totalDelta * 100)),
		Irq:       gtZero(round((curTimes.Irq - lastTimes.Irq) / totalDelta * 100)),
		Softirq:   gtZero(round((curTimes.Softirq - lastTimes.Softirq) / totalDelta * 100)),
		Steal:     gtZero(round((curTimes.Steal - lastTimes.Steal) / totalDelta * 100)),
		Guest:     gtZero(round((curTimes.Guest - lastTimes.Guest) / totalDelta * 100)),
		GuestNice: gtZero(round((curTimes.GuestNice - lastTimes.GuestNice) / totalDelta * 100)),
		Stolen:    gtZero(round((curTimes.Stolen - lastTimes.Stolen) / totalDelta * 100)),
	}
}

// Helper function for rounding to two decimal places
func round(f float64) float64 {
	shift := math.Pow(10, float64(2))
	return math.Floor(f*shift+.5) / shift
}

// Helper function: replace all negative numbers with zero
func gtZero(n float64) float64 {
	if math.Signbit(n) {
		return 0
	}
	return n
}
