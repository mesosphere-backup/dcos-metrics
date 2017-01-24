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

import "testing"

var mockFS = filesystemMetrics{
	fsMetrics: []filesystemMetric{
		filesystemMetric{
			path:        "/",
			capTotal:    uint64(449660412 * 1024),
			capUsed:     uint64(25819220 * 1024),
			capFree:     uint64(423841192 * 1024),
			inodesTotal: uint64(458961 * 1024),
			inodesUsed:  uint64(191925 * 1024),
			inodesFree:  uint64(267036 * 1024),
			timestamp:   "2009-11-10T23:00:00Z",
		},
		filesystemMetric{
			path:        "/boot",
			capTotal:    uint64(449660412 * 1024),
			capUsed:     uint64(25819220 * 1024),
			capFree:     uint64(423841192 * 1024),
			inodesTotal: uint64(458961 * 1024),
			inodesUsed:  uint64(191925 * 1024),
			inodesFree:  uint64(267036 * 1024),
			timestamp:   "2009-11-10T23:00:00Z",
		},
	},
}

func TestFilesystemAddDatapoints(t *testing.T) {
	mockNc := nodeCollector{}

	dps, err := mockFS.getDatapoints()

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(dps) != 12 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
