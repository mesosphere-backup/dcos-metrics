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

func (c *Collector) createContainerDatapoints(container agentContainer) []producers.Datapoint {
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

	addDps := []producers.Datapoint{
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
			Name:  "mem.limit",
			Value: container.Statistics.MemLimitBytes,
			Unit:  bytes,
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

	return dps
}

// -- helpers

func thisTime() string {
	return time.Now().UTC().Format(time.RFC3339Nano)
}
