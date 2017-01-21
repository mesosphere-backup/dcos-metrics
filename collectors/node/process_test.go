package node

import "testing"

var mockProcess = processMetrics{
	processCount: 3,
	timestamp:    "2009-11-10T23:00:00Z",
}

func TestProcessAddDatapoints(t *testing.T) {

	mockNc := nodeCollector{}

	err := mockProcess.addDatapoints(&mockNc)

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(mockNc.datapoints) != 1 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
