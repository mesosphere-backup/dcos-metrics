package node

import (
	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/host"
)

type processMetrics struct {
	processCount uint64
	timestamp    string
}

func (m *processMetrics) poll() error {
	procs, err := getProcessCount()
	if err != nil {
		return err
	}

	m.processCount = procs
	m.timestamp = thisTime()
	return nil
}

func (m *processMetrics) addDatapoints(nc *nodeCollector) error {
	nc.datapoints = append(nc.datapoints, producers.Datapoint{
		Name:      PROCESS_COUNT,
		Unit:      COUNT,
		Value:     m.processCount,
		Timestamp: m.timestamp,
	})

	return nil
}

func getProcessCount() (uint64, error) {
	i, err := host.Info()
	if err != nil {
		return 0, err
	}

	return i.Procs, nil
}
