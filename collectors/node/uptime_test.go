package node

import "testing"

var mockUptime = uptimeMetric{
	timestamp: "2009-11-10T23:00:00Z", uptime: uint64(7865),
}

func TestUptimeAddDatapoints(t *testing.T) {

	mockNc := nodeCollector{}

	err := mockUptime.addDatapoints(&mockNc)

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(mockNc.datapoints) != 1 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
