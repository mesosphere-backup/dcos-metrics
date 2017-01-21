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

	err := mockLoad.addDatapoints(&mockNc)

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(mockNc.datapoints) != 3 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
