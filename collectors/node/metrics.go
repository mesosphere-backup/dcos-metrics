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

	/* Metric name constants */
	UPTIME = "uptime"

	/* process.<namespace> */
	PROCESS_COUNT = "process.count"

	/* load.<namespace> */
	LOAD_1MIN  = "load.1min"
	LOAD_5MIN  = "load.5min"
	LOAD_15MIN = "load.15min"

	/* load.<namespace> */
	CPU_CORES  = "cpu.cores"
	CPU_TOTAL  = "cpu.total"
	CPU_USER   = "cpu.user"
	CPU_SYSTEM = "cpu.system"
	CPU_IDLE   = "cpu.idle"
	CPU_WAIT   = "cpu.wait"

	/* filesystem.<namespace> */
	FS_CAP_TOTAL   = "filesystem.capacity.total"
	FS_CAP_USED    = "filesystem.capacity.used"
	FS_CAP_FREE    = "filesystem.capacity.free"
	FS_INODE_TOTAL = "filesystem.inode.total"
	FS_INODE_USED  = "filesystem.inode.used"
	FS_INODE_FREE  = "filesystem.inode.free"

	/* network.<namespace> */
	NET_IN          = "network.in"
	NET_OUT         = "network.out"
	NET_IN_PACKETS  = "network.in.packets"
	NET_OUT_PACKETS = "network.out.packets"
	NET_IN_DROPPED  = "network_in_dropped"
	NET_OUT_DROPPED = "network_out_dropped"
	NET_IN_ERRORS   = "network_in_errors"
	NET_OUT_ERRORS  = "network_out_errors"

	/* memory.<namespace> */
	MEM_TOTAL   = "memory.total"
	MEM_FREE    = "memory.free"
	MEM_BUFFERS = "memory.buffers"
	MEM_CACHED  = "memory.cached"

	/* swap.<namespace> */
	SWAP_TOTAL = "swap.total"
	SWAP_FREE  = "swap.free"
	SWAP_USED  = "swap.used"
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
