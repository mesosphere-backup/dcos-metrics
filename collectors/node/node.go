// Copyright 2016 Mesosphere, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package node

import (
	"time"

	log "github.com/sirupsen/logrus"

	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
)

// Collector defines the collector type for system-level metrics.
type Collector struct {
	PollPeriod  time.Duration `yaml:"poll_period,omitempty"`
	MetricsChan chan producers.MetricsMessage

	// Specifying a field of type *logrus.Entry allows us to create a single
	// logger for this struct, such as logrus.WithFields(). This way, instead of
	// using a global variable for a logger instance, we can do something like
	// c.log.Errorf(). For more info, see the upstream docs at
	// https://godoc.org/github.com/sirupsen/logrus#Entry
	log *log.Entry

	nodeInfo    collectors.NodeInfo
	nodeMetrics []producers.Datapoint
	timestamp   int64
}

// New returns a new instance of the node metrics collector and a metrics chan.
func New(cfg Collector, nodeInfo collectors.NodeInfo) (Collector, chan producers.MetricsMessage) {
	c := Collector{
		PollPeriod:  cfg.PollPeriod,
		MetricsChan: make(chan producers.MetricsMessage),
		log:         log.WithFields(log.Fields{"collector": "node"}),
		nodeInfo:    nodeInfo,
	}
	return c, c.MetricsChan
}

// RunPoller periodiclly polls the HTTP APIs of a Mesos agent. This function
// should be run in its own goroutine.
func (c *Collector) RunPoller() {
	for {
		c.pollHost()
		for _, m := range c.transform() {
			c.MetricsChan <- m
		}
		time.Sleep(c.PollPeriod)
	}
}

// pollHost queries the DC/OS hsot for metrics and returns.
func (c *Collector) pollHost() {
	now := time.Now().UTC()
	c.timestamp = now.Unix()

	// Fetch node-level metrics for all DC/OS roles
	c.log.Debugf("Fetching node-level metrics from DC/OS host %s", c.nodeInfo.Hostname)
	nm, err := getNodeMetrics()
	if err != nil {
		c.log.Errorf("Failed to get node-level metrics. %s", err)
	} else {
		c.nodeMetrics = nm
	}

	c.log.Infof("Finished polling DC/OS host %s, took %f seconds.", c.nodeInfo.Hostname, time.Since(now).Seconds())
}

// transform will take metrics retrieved from the agent and perform any
// transformations necessary to make the data fit the output expected by
// producers.MetricsMessage.
func (c *Collector) transform() (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	t := time.Unix(c.timestamp, 0)

	// Produce node metrics
	msg = producers.MetricsMessage{
		Name:       producers.NodeMetricPrefix,
		Datapoints: c.nodeMetrics,
		Dimensions: producers.Dimensions{
			MesosID:   c.nodeInfo.MesosID,
			ClusterID: c.nodeInfo.ClusterID,
			Hostname:  c.nodeInfo.Hostname,
		},
		Timestamp: t.UTC().Unix(),
	}
	out = append(out, msg)

	return out
}
