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

	err := mockMemory.addDatapoints(&mockNc)

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(mockNc.datapoints) != 7 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
