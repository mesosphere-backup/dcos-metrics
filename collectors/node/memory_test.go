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

var mockMemory = memoryMetric{
	memTotal:   uint64(16310888 * 1024),
	memFree:    uint64(9121592 * 1024),
	memBuffers: uint64(101784 * 1024),
	memCached:  uint64(2909412 * 1024),
	swapTotal:  uint64(16658428 * 1024),
	swapFree:   uint64(16658421 * 1024),
	swapUsed:   uint64(1 * 1024),
	timestamp:  "2009-11-10T23:00:00Z",
}

func TestMemoryAddDatapoints(t *testing.T) {
	mockNc := nodeCollector{}

	dps, err := mockMemory.getDatapoints()

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(dps) != 7 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
