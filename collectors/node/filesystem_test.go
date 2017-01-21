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

	err := mockFS.addDatapoints(&mockNc)

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(mockNc.datapoints) != 12 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
