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

var mockLoad = loadMetric{
	load1Min:  float64(0.42),
	load5Min:  float64(0.58),
	load15Min: float64(0.57),
	timestamp: "2009-11-10T23:00:00Z",
}

func TestLoadAddDatapoints(t *testing.T) {

	mockNc := nodeCollector{}

	dps, err := mockLoad.getDatapoints()

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(dps) != 3 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
