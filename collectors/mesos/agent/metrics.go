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
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/dcos/dcos-metrics/producers"
)

const (
	// Container tagging constants

	containerID  = "container_id"
	source       = "source"
	frameworkID  = "framework_id"
	executorID   = "executor_id"
	executorName = "executor_name"
	blkioDevice  = "blkio_device"

	// Container unit constants
	seconds = "seconds"
	count   = "count"
	bytes   = "bytes"

	// Namespaces of blkio isolation strategy
	blkioCfq          = "blkio.cfq"
	blkioCfqRecursive = "blkio.cfq_recursive"
	blkioThrottling   = "blkio.throttling"

	// All blkio statistics
	serviced     = "io_serviced"
	serviceBytes = "io_service_bytes"
	serviceTime  = "io_service_time"
	merged       = "io_merged"
	queued       = "io_queued"
	waitTime     = "io_wait_time"

	// The device name for 'total' blkio stats
	devTotal = "total"
)

// ErrNoStatistics is a sentinel error for cases where the mesos /containers
// endpoint returns a container member which has no statistics hash. It's
// undesirable but not an error case, so we just warn.
var ErrNoStatistics = errors.New("No statistics found")

func (c *Collector) createContainerDatapoints(container agentContainer) ([]producers.Datapoint, error) {
	ts := thisTime()
	dps := []producers.Datapoint{}

	dpTags := map[string]string{
		containerID:  container.ContainerID,
		source:       container.Source,
		frameworkID:  container.FrameworkID,
		executorID:   container.ExecutorID,
		executorName: container.ExecutorName,
	}

	c.log.Debugf("Adding tags for container %s:\n%+v", container.ContainerID, dpTags)

	if container.Statistics == nil {
		return dps, ErrNoStatistics
	}

	addDps := []producers.Datapoint{
		producers.Datapoint{
			Name:  "processes",
			Value: container.Statistics.Processes,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "threads",
			Value: container.Statistics.Threads,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.user.time",
			Value: container.Statistics.CpusUserTimeSecs,
			Unit:  seconds,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.system.time",
			Value: container.Statistics.CpusSystemTimeSecs,
			Unit:  seconds,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.limit",
			Value: container.Statistics.CpusLimit,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.nr.periods",
			Value: container.Statistics.CpusNrPeriods,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.nr.throttled",
			Value: container.Statistics.CpusNrThrottled,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.throttled.time",
			Value: container.Statistics.CpusThrottledTimeSecs,
			Unit:  seconds,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.total",
			Value: container.Statistics.MemTotalBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.total.memsw",
			Value: container.Statistics.MemTotalMemswBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.limit",
			Value: container.Statistics.MemLimitBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.soft.limit",
			Value: container.Statistics.MemSoftLimitBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.cache",
			Value: container.Statistics.MemCacheBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.rss",
			Value: container.Statistics.MemRssBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.mapped.file",
			Value: container.Statistics.MemMappedFileBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.swap",
			Value: container.Statistics.MemSwapBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.unevictable",
			Value: container.Statistics.MemUnevictableBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.low.pressure",
			Value: container.Statistics.MemLowPressureCounter,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.medium.pressure",
			Value: container.Statistics.MemMediumPressureCounter,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.critical.pressure",
			Value: container.Statistics.MemCriticalPressureCounter,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "disk.limit",
			Value: container.Statistics.DiskLimitBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "disk.used",
			Value: container.Statistics.DiskUsedBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.rx.packets",
			Value: container.Statistics.NetRxPackets,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.rx.bytes",
			Value: container.Statistics.NetRxBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.rx.errors",
			Value: container.Statistics.NetRxErrors,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.rx.dropped",
			Value: container.Statistics.NetRxDropped,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.tx.packets",
			Value: container.Statistics.NetRxPackets,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.tx.bytes",
			Value: container.Statistics.NetRxBytes,
			Unit:  bytes,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.tx.errors",
			Value: container.Statistics.NetRxErrors,
			Unit:  count,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.tx.dropped",
			Value: container.Statistics.NetRxDropped,
			Unit:  count,
			Tags:  dpTags,
		},
	}

	for _, dp := range addDps {
		dp.Timestamp = ts
		dps = append(dps, dp)
	}

	// It's possible that the blkio object was not present
	if container.Statistics.Blkio == nil {
		return dps, nil
	}

	// Blkio stats are nested, and need to be processed separately
	if container.Statistics.Blkio.Cfq != nil {
		stats := deviceStatsToDatapoints(container.Statistics.Blkio.Cfq, blkioCfq, dpTags)
		dps = append(dps, stats...)
	}
	if container.Statistics.Blkio.CfqRecursive != nil {
		stats := deviceStatsToDatapoints(container.Statistics.Blkio.CfqRecursive, blkioCfqRecursive, dpTags)
		dps = append(dps, stats...)
	}
	if container.Statistics.Blkio.Throttling != nil {
		stats := deviceStatsToDatapoints(container.Statistics.Blkio.Throttling, blkioThrottling, dpTags)
		dps = append(dps, stats...)
	}

	return dps, nil
}

// deviceStatsToDatapoints flattens the nested blkio stats into a list of
// clearly-named datapoints. All units are bytes, therefore all values are
// integers. Datapoints from throttled devices are tagged with their origin.
func deviceStatsToDatapoints(stats []IODeviceStats, prefix string, baseTags map[string]string) []producers.Datapoint {
	var datapoints []producers.Datapoint

	for _, stat := range stats {
		dev := devTotal
		if stat.Device.Major > 0 {
			dev = fmt.Sprintf("%d:%d", stat.Device.Major, stat.Device.Minor)
		}
		// Create a new tags map for these datapoints
		tags := map[string]string{blkioDevice: dev}
		for k, v := range baseTags {
			tags[k] = v
		}
		datapoints = append(datapoints, statValuesToDatapoints(stat.Serviced, prefix+"."+serviced, tags)...)
		datapoints = append(datapoints, statValuesToDatapoints(stat.ServiceBytes, prefix+"."+serviceBytes, tags)...)
		datapoints = append(datapoints, statValuesToDatapoints(stat.ServiceTime, prefix+"."+serviceTime, tags)...)
		datapoints = append(datapoints, statValuesToDatapoints(stat.Merged, prefix+"."+merged, tags)...)
		datapoints = append(datapoints, statValuesToDatapoints(stat.Queued, prefix+"."+queued, tags)...)
		datapoints = append(datapoints, statValuesToDatapoints(stat.WaitTime, prefix+"."+waitTime, tags)...)
	}

	return datapoints
}

// statValuesToDatapoints converts a list of IO stats to a list of datapoints
func statValuesToDatapoints(vals []IOStatValue, prefix string, tags map[string]string) []producers.Datapoint {
	var datapoints []producers.Datapoint
	for _, s := range vals {
		op := strings.ToLower(s.Operation)
		d := producers.Datapoint{
			Name:  prefix + "." + op,
			Value: s.Value,
			Tags:  tags,
		}
		datapoints = append(datapoints, d)
	}
	return datapoints
}

// -- helpers

func thisTime() string {
	return time.Now().UTC().Format(time.RFC3339Nano)
}
