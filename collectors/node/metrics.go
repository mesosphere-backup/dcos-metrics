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
	"runtime"
	"time"

	"github.com/dcos/dcos-metrics/producers"
)

const (
	// Unit constants
	countUnit   = "count"
	bytesUnit   = "bytes"
	percentUnit = "percent"

	/* Metric name constants */
	uptimeMetricName = "system.uptime"

	/* process.<namespace> */
	processCount = "process.count"

	/* load.<namespace> */
	load1Min  = "load.1min"
	load5Min  = "load.5min"
	load15Min = "load.15min"

	/* load.<namespace> */
	cpuCores  = "cpu.cores"
	cpuTotal  = "cpu.total"
	cpuUser   = "cpu.user"
	cpuSystem = "cpu.system"
	cpuIdle   = "cpu.idle"
	cpuWait   = "cpu.wait"

	/* filesystem.<namespace> */
	fsCapTotal   = "filesystem.capacity.total"
	fsCapUsed    = "filesystem.capacity.used"
	fsCapFree    = "filesystem.capacity.free"
	fsInodeTotal = "filesystem.inode.total"
	fsInodeUsed  = "filesystem.inode.used"
	fsInodeFree  = "filesystem.inode.free"

	/* network.<namespace> */
	netIn         = "network.in"
	netOut        = "network.out"
	netInPackets  = "network.in.packets"
	netOutPackets = "network.out.packets"
	netInDropped  = "network.in.dropped"
	netOutDropped = "network.out.dropped"
	netInErrors   = "network.in.errors"
	netOutErrors  = "network.out.errors"

	/* memory.<namespace> */
	memTotal   = "memory.total"
	memFree    = "memory.free"
	memBuffers = "memory.buffers"
	memCached  = "memory.cached"

	/* swap.<namespace> */
	swapTotal = "swap.total"
	swapFree  = "swap.free"
	swapUsed  = "swap.used"
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
			if runtime.GOOS == "windows" {
				// Some of the pollers may not be implemented on Windows so skip over them.
				// For instance, loadMetrics is not implemented.
				continue
			}

			return nc.datapoints, err
		}

		dps, err := mp.getDatapoints()
		if err != nil {
			return nc.datapoints, err
		}
		nc.datapoints = append(nc.datapoints, dps...)
	}

	return nc.datapoints, nil
}

// -- helpers

func thisTime() string {
	return time.Now().UTC().Format(time.RFC3339Nano)
}
