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

	// Container unit constants
	seconds = "seconds"
	count   = "count"
	bytes   = "bytes"
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

	return dps, nil
}

// -- helpers

func thisTime() string {
	return time.Now().UTC().Format(time.RFC3339Nano)
}
