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

var mockProcess = processMetrics{
	processCount: 3,
	timestamp:    "2009-11-10T23:00:00Z",
}

func TestProcessAddDatapoints(t *testing.T) {

	mockNc := nodeCollector{}

	dps, err := mockProcess.getDatapoints()

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(dps) != 1 {
		t.Error("Expected 1 process metric datapoint, got", len(mockNc.datapoints))
	}

}

func TestGetProcMetric(t *testing.T) {
	tpm, err := getProcMetrics()

	if err != nil {
		t.Error("expected no errors getting process metrics, got", err.Error())
	}

	if len(tpm) == 0 {
		t.Error("expected more than one process metric, got", len(tpm))
	}
}

func TestGetRunningProcesses(t *testing.T) {
	rp, err := getRunningProcesses()

	if err != nil {
		t.Error("expected no errors getting running processes, got", err.Error())
	}

	if len(rp) == 0 {
		t.Error("expected more than 0 running processes, got", len(rp))
	}
}

func TestStringify(t *testing.T) {
	s := stringify(float64(23.45))
	if s != "23.45" {
		t.Error("expected \"23.45\", got", s)
	}
}
