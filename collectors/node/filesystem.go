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
	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/disk"
)

type filesystemMetrics struct {
	fsMetrics []filesystemMetric
}

type filesystemMetric struct {
	path        string
	capTotal    uint64
	capUsed     uint64
	capFree     uint64
	inodesTotal uint64
	inodesUsed  uint64
	inodesFree  uint64
	timestamp   string
}

func (m *filesystemMetrics) poll() error {
	f := []filesystemMetric{}

	ts := thisTime()
	parts, err := disk.Partitions(false) // only phsysical partitions
	if err != nil {
		return err
	}

	for _, part := range parts {
		usage, err := disk.Usage(part.Mountpoint)
		if err != nil {
			return err
		}

		f = append(f, filesystemMetric{
			path:        usage.Path,
			capTotal:    usage.Total,
			capUsed:     usage.Used,
			capFree:     usage.Free,
			inodesTotal: usage.InodesTotal,
			inodesUsed:  usage.InodesUsed,
			inodesFree:  usage.InodesFree,
			timestamp:   ts,
		})
	}

	m.fsMetrics = f
	return nil
}

func (m *filesystemMetrics) addDatapoints(nc *nodeCollector) error {
	/* Enumerate each filesystem found and add a datapoint object contining the
	capacity and inode metrics plus a tag denoting the filesystem
	path from which these came */
	for _, fs := range m.fsMetrics {
		nc.datapoints = append(nc.datapoints, producers.Datapoint{
			Name:      FS_CAP_TOTAL,
			Unit:      BYTES,
			Value:     fs.capTotal,
			Timestamp: fs.timestamp,
			Tags: map[string]string{
				"path": fs.path,
			},
		})
		nc.datapoints = append(nc.datapoints, producers.Datapoint{
			Name:      FS_CAP_USED,
			Unit:      BYTES,
			Value:     fs.capUsed,
			Timestamp: fs.timestamp,
			Tags: map[string]string{
				"path": fs.path,
			},
		})
		nc.datapoints = append(nc.datapoints, producers.Datapoint{
			Name:      FS_CAP_FREE,
			Unit:      BYTES,
			Value:     fs.capFree,
			Timestamp: fs.timestamp,
			Tags: map[string]string{
				"path": fs.path,
			},
		})
		nc.datapoints = append(nc.datapoints, producers.Datapoint{
			Name:      FS_INODE_TOTAL,
			Unit:      COUNT,
			Value:     fs.inodesTotal,
			Timestamp: fs.timestamp,
			Tags: map[string]string{
				"path": fs.path,
			},
		})
		nc.datapoints = append(nc.datapoints, producers.Datapoint{
			Name:      FS_INODE_USED,
			Unit:      COUNT,
			Value:     fs.inodesUsed,
			Timestamp: fs.timestamp,
			Tags: map[string]string{
				"path": fs.path,
			},
		})
		nc.datapoints = append(nc.datapoints, producers.Datapoint{
			Name:      FS_INODE_FREE,
			Unit:      COUNT,
			Value:     fs.inodesFree,
			Timestamp: fs.timestamp,
			Tags: map[string]string{
				"path": fs.path,
			},
		})
	}
	return nil
}
