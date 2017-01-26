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
	"fmt"
	"io"
	"os"
	"strconv"

	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/host"
	"github.com/shirou/gopsutil/process"
)

type processMetrics struct {
	processCount uint64
	processes    []processMetric
	timestamp    string
}

type processMetric struct {
	name    string
	pid     int32
	ppid    int32
	openFDs int32
	// CPU time metrics
	cpuTimeSystem float64
	cpuTimeUser   float64
	cpuTimeIdle   float64
	createTime    int64
	// See https://github.com/shirou/gopsutil/blob/master/process/process_linux.go#L146
	// Status returns the process status.
	// Return value could be one of these.
	// R: Running S: Sleep T: Stop I: Idle
	// Z: Zombie W: Wait L: Lock
	// The charactor is same within all supported platforms.
	status string
}

func (m *processMetrics) poll() error {
	numProcs, err := getProcessCount()
	if err != nil {
		return err
	}
	m.processCount = numProcs

	processes, err := getProcMetrics()
	if err != nil {
		return err
	}
	m.processes = processes

	m.timestamp = thisTime()
	return nil
}

func (m *processMetrics) getDatapoints() ([]producers.Datapoint, error) {
	dps := []producers.Datapoint{
		producers.Datapoint{
			Name:      PROCESS_COUNT,
			Unit:      COUNT,
			Value:     m.processCount,
			Timestamp: m.timestamp,
		}}

	for _, proc := range m.processes {
		dps = append(dps, producers.Datapoint{
			Name:      PROCESS_PID,
			Value:     proc.pid,
			Timestamp: m.timestamp,
			Tags: map[string]string{
				"name":            proc.name,
				"open_fd":         stringify(proc.openFDs),
				"parent_pid":      stringify(proc.ppid),
				"status":          proc.status,
				"cpu_time_system": stringify(proc.cpuTimeSystem),
				"cpu_time_user":   stringify(proc.cpuTimeUser),
				"cpu_time_idle":   stringify(proc.cpuTimeIdle),
			},
		})
	}

	return dps, nil
}

func getProcessCount() (uint64, error) {
	i, err := host.Info()
	if err != nil {
		return 0, err
	}

	return i.Procs, nil
}

func getProcMetrics() ([]processMetric, error) {
	var pm = []processMetric{}

	runningProcs, err := getRunningProcesses()
	if err != nil {
		return pm, err
	}

	for _, proc := range runningProcs {
		thisProc := processMetric{}

		procInfo, err := process.NewProcess(proc)
		if err != nil {
			// If NewProcess fails is means the process no longer exists, we skip
			// this process and move on to the next.
			continue
		}

		thisProc.pid = procInfo.Pid

		PPid, err := procInfo.Ppid()
		if err != nil {
			return pm, err
		}
		thisProc.ppid = PPid

		name, err := procInfo.Name()
		if err != nil {
			return pm, err
		}
		thisProc.name = name

		status, err := procInfo.Status()
		if err != nil {
			return pm, err
		}
		thisProc.status = status

		numfds, err := procInfo.NumFDs()
		if err != nil {
			return pm, err
		}
		thisProc.openFDs = numfds

		created, err := procInfo.CreateTime()
		if err != nil {
			return pm, err
		}
		thisProc.createTime = created

		cpuTimes, err := procInfo.Times()
		if err != nil {
			return pm, err
		}
		thisProc.cpuTimeSystem = cpuTimes.System
		thisProc.cpuTimeUser = cpuTimes.User
		thisProc.cpuTimeIdle = cpuTimes.Idle

		pm = append(pm, thisProc)
	}
	return pm, nil
}

// Ripped from https://raw.githubusercontent.com/mitchellh/go-ps/master/process_unix.go
func getRunningProcesses() ([]int32, error) {
	d, err := os.Open("/proc")
	if err != nil {
		return nil, err
	}
	defer d.Close()

	results := []int32{}
	for {
		fis, err := d.Readdir(10)
		if err == io.EOF {
			break
		}
		if err != nil {
			fmt.Println("ERROR reading file")
			return nil, err
		}

		for _, fi := range fis {
			// We only care about directories, since all pids are dirs
			if !fi.IsDir() {
				continue
			}

			// We only care if the name starts with a numeric
			name := fi.Name()
			if name[0] < '0' || name[0] > '9' {
				continue
			}

			// From this point forward, any errors we just ignore, because
			// it might simply be that the process doesn't exist anymore.
			pid, err := strconv.ParseInt(name, 10, 0)
			if err != nil {
				continue
			}

			results = append(results, int32(pid))
		}
	}

	return results, nil
}

func stringify(thisString interface{}) string {
	return fmt.Sprintf("%v", thisString)
}
