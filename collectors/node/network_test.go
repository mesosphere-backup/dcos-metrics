package node

import "testing"

var (
	mockNet = networkMetrics{
		interfaces: []networkMetric{
			networkMetric{
				interfaceName: "eth0",
				netIn:         uint64(260522667),
				netOut:        uint64(10451619),
				netInPackets:  uint64(1058595),
				netOutPackets: uint64(62547),
				netInDropped:  uint64(99),
				netOutDropped: uint64(99),
				netInErrors:   uint64(99),
				netOutErrors:  uint64(99),
				timestamp:     "2009-11-10T23:00:00Z",
			},
			networkMetric{
				interfaceName: "slv1",
				netIn:         uint64(260522667),
				netOut:        uint64(10451619),
				netInPackets:  uint64(1058595),
				netOutPackets: uint64(62547),
				netInDropped:  uint64(99),
				netOutDropped: uint64(99),
				netInErrors:   uint64(99),
				netOutErrors:  uint64(99),
				timestamp:     "2009-11-10T23:00:00Z",
			},
		},
	}
)

func TestNetworkAddDatapoints(t *testing.T) {
	mockNc := nodeCollector{}

	err := mockNet.addDatapoints(&mockNc)

	if err != nil {
		t.Errorf("Expected no errors getting datapoints from mockCPU, got %s", err.Error())
	}

	if len(mockNc.datapoints) != 16 {
		t.Error("Expected 6 CPU metric datapoints, got", len(mockNc.datapoints))
	}

}
