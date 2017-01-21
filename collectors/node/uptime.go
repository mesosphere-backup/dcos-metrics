package node

import (
	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/host"
)

type uptimeMetric struct {
	uptime    uint64
	timestamp string
}

func (m *uptimeMetric) poll() error {
	uptime, err := host.Uptime()
	if err != nil {
		return err
	}
	m.uptime = uptime
	m.timestamp = thisTime()
	return nil
}

func (m *uptimeMetric) addDatapoints(nc *nodeCollector) error {
	nc.datapoints = append(nc.datapoints, producers.Datapoint{
		Name:      UPTIME,
		Unit:      COUNT,
		Value:     m.uptime,
		Timestamp: m.timestamp,
	})
	return nil
}
