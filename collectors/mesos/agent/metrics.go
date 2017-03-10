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
	CONTAINERID  = "container_id"
	SOURCE       = "source"
	FRAMEWORKID  = "framework_id"
	EXECUTORID   = "executor_id"
	EXECUTORNAME = "executor_name"

	// Container unit constants
	SECONDS = "seconds"
	COUNT   = "count"
	BYTES   = "bytes"
)

func (c *Collector) createContainerDatapoints(container agentContainer) []producers.Datapoint {
	ts := thisTime()
	dps := []producers.Datapoint{}

	dpTags := map[string]string{
		CONTAINERID:  container.ContainerID,
		SOURCE:       container.Source,
		FRAMEWORKID:  container.FrameworkID,
		EXECUTORID:   container.ExecutorID,
		EXECUTORNAME: container.ExecutorName,
	}

	c.log.Debugf("Adding tags for container %s:\n%+v", container.ContainerID, dpTags)

	addDps := []producers.Datapoint{
		producers.Datapoint{
			Name:  "cpus.user.time",
			Value: container.Statistics.CpusUserTimeSecs,
			Unit:  SECONDS,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.system.time",
			Value: container.Statistics.CpusSystemTimeSecs,
			Unit:  SECONDS,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.limit",
			Value: container.Statistics.CpusLimit,
			Unit:  COUNT,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "cpus.throttled.time",
			Value: container.Statistics.CpusThrottledTimeSecs,
			Unit:  SECONDS,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.total",
			Value: container.Statistics.MemTotalBytes,
			Unit:  BYTES,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "mem.limit",
			Value: container.Statistics.MemLimitBytes,
			Unit:  BYTES,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "disk.limit",
			Value: container.Statistics.DiskLimitBytes,
			Unit:  BYTES,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "disk.used",
			Value: container.Statistics.DiskUsedBytes,
			Unit:  BYTES,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.rx.packets",
			Value: container.Statistics.NetRxPackets,
			Unit:  COUNT,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.rx.bytes",
			Value: container.Statistics.NetRxBytes,
			Unit:  BYTES,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.rx.errors",
			Value: container.Statistics.NetRxErrors,
			Unit:  COUNT,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.rx.dropped",
			Value: container.Statistics.NetRxDropped,
			Unit:  COUNT,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.tx.packets",
			Value: container.Statistics.NetRxPackets,
			Unit:  COUNT,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.tx.bytes",
			Value: container.Statistics.NetRxBytes,
			Unit:  BYTES,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.tx.errors",
			Value: container.Statistics.NetRxErrors,
			Unit:  COUNT,
			Tags:  dpTags,
		},
		producers.Datapoint{
			Name:  "net.tx.dropped",
			Value: container.Statistics.NetRxDropped,
			Unit:  COUNT,
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
