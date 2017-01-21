package node

import "testing"

var (
	mockCpuMetric = cpuCoresMetric{
		cpuCores:  4,
		cpuTotal:  float64(12.50),
		cpuUser:   float64(9.60),
		cpuSystem: float64(2.90),
		cpuIdle:   float64(87.40),
		cpuWait:   float64(0.10),
		timestamp: "2009-11-10T23:00:00Z",
	}
)

func TestCPUAddDatapoints(t *testing.T) {

	mockNc := nodeCollector{}

	err := mockCpuMetric.addDatapoints(&mockNc)

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(mockNc.datapoints) != 6 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
