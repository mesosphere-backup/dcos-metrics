package node

import (
	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/mem"
)

type memoryMetric struct {
	memTotal   uint64
	memFree    uint64
	memBuffers uint64
	memCached  uint64
	swapTotal  uint64
	swapFree   uint64
	swapUsed   uint64
	timestamp  string
}

func (m *memoryMetric) poll() error {
	ts := thisTime()
	m.timestamp = ts

	mem, err := getMemory()
	if err != nil {
		return err
	}

	m.memTotal = mem.Total
	m.memFree = mem.Free
	m.memBuffers = mem.Buffers
	m.memCached = mem.Cached

	swap, err := getSwap()
	if err != nil {
		return err
	}

	m.swapTotal = swap.Total
	m.swapFree = swap.Free
	m.swapUsed = swap.Used

	return nil
}

func (m *memoryMetric) addDatapoints(nc *nodeCollector) error {
	memDps := []producers.Datapoint{
		producers.Datapoint{
			Name:      MEM_TOTAL,
			Unit:      BYTES,
			Value:     m.memTotal,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      MEM_FREE,
			Unit:      BYTES,
			Value:     m.memFree,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      MEM_BUFFERS,
			Unit:      BYTES,
			Value:     m.memBuffers,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      MEM_CACHED,
			Unit:      BYTES,
			Value:     m.memCached,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      SWAP_TOTAL,
			Unit:      BYTES,
			Value:     m.swapTotal,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      SWAP_FREE,
			Unit:      BYTES,
			Value:     m.swapFree,
			Timestamp: m.timestamp,
		},
		producers.Datapoint{
			Name:      SWAP_USED,
			Unit:      BYTES,
			Value:     m.swapUsed,
			Timestamp: m.timestamp,
		},
	}

	for _, dp := range memDps {
		nc.datapoints = append(nc.datapoints, dp)
	}

	return nil
}

/* Helpers */

func getMemory() (*mem.VirtualMemoryStat, error) {
	m, err := mem.VirtualMemory()
	return m, err
}

func getSwap() (*mem.SwapMemoryStat, error) {
	s, err := mem.SwapMemory()
	return s, err
}
