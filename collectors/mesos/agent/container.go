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

package agent

import (
	"net"
	"net/url"
	"strconv"

	"github.com/dcos/dcos-metrics/util/http/client"
)

// agentContainer defines the structure of the response expected from Mesos
// *for a single container* when polling the '/containers' endpoint in API v1.
// Note that agentContainer is actually in a top-level array. For more info, see
// https://github.com/apache/mesos/blob/1.0.1/include/mesos/v1/agent/agent.proto#L161-L168
type agentContainer struct {
	FrameworkID     string                 `json:"framework_id"`
	ExecutorID      string                 `json:"executor_id"`
	ExecutorName    string                 `json:"executor_name"`
	Source          string                 `json:"source"`
	ContainerID     string                 `json:"container_id"`
	ContainerStatus map[string]interface{} `json:"container_status"`
	Statistics      *resourceStatistics    `json:"statistics"`
}

// resourceStatistics defines the structure of the response expected from Mesos
// when referring to container and/or executor metrics. In Mesos, the
// ResourceStatistics message is very large; it defines many fields that are
// dependent on a feature being enabled in Mesos, and not all of those features
// are enabled in DC/OS.
//
// Therefore, we redefine the resourceStatistics struct here with only the fields
// dcos-metrics currently cares about, which should be stable for Mesos API v1.
//
// For a complete reference, see:
// https://github.com/apache/mesos/blob/1.0.1/include/mesos/v1/mesos.proto#L921-L1022
type resourceStatistics struct {

	// Process and thread info
	Processes uint32 `json:"processes,omitempty"`
	Threads   uint32 `json:"threads,omitempty"`

	// CPU usage info
	CpusUserTimeSecs      float64 `json:"cpus_user_time_secs,omitempty"`
	CpusSystemTimeSecs    float64 `json:"cpus_system_time_secs,omitempty"`
	CpusLimit             float64 `json:"cpus_limit,omitempty"`
	CpusNrPeriods         uint32  `json:"cpus_nr_periods,omitempty"`
	CpusNrThrottled       uint32  `json:"cpus_nr_throttled,omitempty"`
	CpusThrottledTimeSecs float64 `json:"cpus_throttled_time_secs,omitempty"`

	// Memory info
	MemTotalBytes              uint64 `json:"mem_total_bytes,omitempty"`
	MemTotalMemswBytes         uint64 `json:"mem_total_memsw_bytes,omitempty"`
	MemLimitBytes              uint64 `json:"mem_limit_bytes,omitempty"`
	MemSoftLimitBytes          uint64 `json:"mem_soft_limit_bytes,omitempty"`
	MemCacheBytes              uint64 `json:"mem_cache_bytes,omitempty"`
	MemRssBytes                uint64 `json:"mem_rss_bytes,omitempty"`
	MemMappedFileBytes         uint64 `json:"mem_mapped_file_bytes,omitempty"`
	MemSwapBytes               uint64 `json:"mem_swap_bytes,omitempty"`
	MemUnevictableBytes        uint64 `json:"mem_unevictable_bytes,omitempty"`
	MemLowPressureCounter      uint64 `json:"mem_low_pressure_counter,omitempty"`
	MemMediumPressureCounter   uint64 `json:"mem_medium_pressure_counter,omitempty"`
	MemCriticalPressureCounter uint64 `json:"mem_critical_pressure_counter,omitempty"`

	// Disk info
	DiskLimitBytes uint64 `json:"disk_limit_bytes,omitempty"`
	DiskUsedBytes  uint64 `json:"disk_used_bytes,omitempty"`

	// Network info
	NetRxPackets uint64 `json:"net_rx_packets,omitempty"`
	NetRxBytes   uint64 `json:"net_rx_bytes,omitempty"`
	NetRxErrors  uint64 `json:"net_rx_errors,omitempty"`
	NetRxDropped uint64 `json:"net_rx_dropped,omitempty"`
	NetTxPackets uint64 `json:"net_tx_packets,omitempty"`
	NetTxBytes   uint64 `json:"net_tx_bytes,omitempty"`
	NetTxErrors  uint64 `json:"net_tx_errors,omitempty"`
	NetTxDropped uint64 `json:"net_tx_dropped,omitempty"`
}

// poll queries an agent for container-level metrics, such as
// CPU, memory, disk, and network usage.
func (c *Collector) getContainerMetrics() error {
	u := url.URL{
		Scheme: c.RequestProtocol,
		Host:   net.JoinHostPort(c.nodeInfo.IPAddress, strconv.Itoa(c.Port)),
		Path:   "/containers",
	}

	c.HTTPClient.Timeout = HTTPTIMEOUT

	return client.Fetch(c.HTTPClient, u, &c.containerMetrics)
}
