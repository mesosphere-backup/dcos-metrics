package node

import (
	"time"

	"github.com/dcos/dcos-metrics/collector"
	"github.com/dcos/dcos-metrics/producers"
)

type NodeCollector struct {
	PollPeriod time.Duration `yaml:"poll_period,omitempty"`

	MetricsChan chan producers.MetricsMessage
	NodeInfo    collector.NodeInfo
}
