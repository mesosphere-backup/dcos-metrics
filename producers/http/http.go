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

package http

import (
	"fmt"
	"net"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/coreos/go-systemd/activation"
	"github.com/dcos/dcos-go/store"
	log "github.com/sirupsen/logrus"

	"github.com/dcos/dcos-metrics/producers"
	prodHelpers "github.com/dcos/dcos-metrics/util/producers"
)

var httpLog = log.WithFields(log.Fields{
	"producer": "http",
})

// Config for the HTTP producer
type Config struct {
	Port        int `yaml:"port"`
	IP          string
	CacheExpiry time.Duration
	DCOSRole    string
	Listener    net.Listener
}

type producerImpl struct {
	config             Config
	store              store.Store
	metricsChan        chan producers.MetricsMessage
	janitorRunInterval time.Duration
}

// New creates a new instance of the HTTP producer with the provided configuration.
func New(cfg Config) (producers.MetricsProducer, chan producers.MetricsMessage) {
	p := producerImpl{
		config:             cfg,
		store:              store.New(),
		metricsChan:        make(chan producers.MetricsMessage),
		janitorRunInterval: 60 * time.Second,
	}
	return &p, p.metricsChan
}

// Run a HTTP server and serve the various metrics API endpoints.
// This function should be run in its own goroutine.
func (p *producerImpl) Run() error {
	httpLog.Info("Starting HTTP producer garbage collection service")
	go p.janitor()

	go func() {
		httpLog.Debug("HTTP producer listening for incoming messages on metricsChan")
		for {
			// read messages off the channel,
			// and give them a unique name in the store
			message := <-p.metricsChan
			httpLog.Debugf("Received message '%+v' with timestamp %s",
				message, time.Unix(message.Timestamp, 0).Format(time.RFC3339))

			var name string
			switch message.Name {
			case producers.NodeMetricPrefix:
				name = joinMetricName(message.Name, message.Dimensions.MesosID)

			case producers.ContainerMetricPrefix:
				name = joinMetricName(message.Name, message.Dimensions.ContainerID)

			case producers.AppMetricPrefix:
				name = joinMetricName(message.Name, message.Dimensions.ContainerID)
			}

			for _, d := range message.Datapoints {
				p.writeObjectToStore(d, message, name)
			}
		}
	}()

	r := newRouter(p)

	// If a listener was provided directly in config, use it.
	if p.config.Listener != nil {
		httpLog.Infof("http producer serving requests on: %s", p.config.Listener.Addr().String())
		return http.Serve(p.config.Listener, r)
	}

	// If a listener from systemd is available, use it.
	listeners, err := activation.Listeners()
	if err != nil {
		return fmt.Errorf("Unable to get listeners: %s", err)
	}
	if len(listeners) == 1 {
		httpLog.Infof("http producer serving requests on systemd socket: %s", listeners[0].Addr().String())
		return http.Serve(listeners[0], r)
	}

	// Listen on the configured TCP port.
	httpLog.Infof("http producer serving requests on tcp socket: %s", net.JoinHostPort(p.config.IP, strconv.Itoa(p.config.Port)))
	return http.ListenAndServe(fmt.Sprintf("%s:%d", p.config.IP, p.config.Port), r)
}

// writeObjectToStore writes a prefixed datapoint into the store.
func (p *producerImpl) writeObjectToStore(d producers.Datapoint, m producers.MetricsMessage, prefix string) {
	newMessage := producers.MetricsMessage{
		Name:       m.Name,
		Datapoints: []producers.Datapoint{d},
		Dimensions: m.Dimensions,
		Timestamp:  m.Timestamp,
	}
	// e.g. dcos.metrics.app.[ContainerId].kafka.server.ReplicaFetcherManager.MaxLag
	qualifiedName := joinMetricName(prefix, d.Name)
	for _, pair := range prodHelpers.SortTags(d.Tags) {
		k, v := pair[0], pair[1]
		// e.g. dcos.metrics.node.[MesosId].network.out.errors.#interface:eth0
		serializedTag := fmt.Sprintf("#%s:%s", k, v)
		qualifiedName = joinMetricName(qualifiedName, serializedTag)
	}
	httpLog.Debugf("Setting store object '%s' with timestamp %s",
		qualifiedName, time.Unix(newMessage.Timestamp, 0).Format(time.RFC3339))
	p.store.Set(qualifiedName, newMessage)
}

// joinMetricName concatenates its arguments using the metric name separator
func joinMetricName(segments ...string) string {
	return strings.Join(segments, producers.MetricNamespaceSep)
}

// janitor analyzes the objects in the store and removes stale objects. An
// object is considered stale when the top-level timestamp of its MetricsMessage
// has exceeded the CacheExpiry, which is calculated as a multiple of the
// collector's polling period. This function should be run in its own goroutine.
func (p *producerImpl) janitor() {
	ticker := time.NewTicker(p.janitorRunInterval)
	for {
		select {
		case _ = <-ticker.C:
			for k, v := range p.store.Objects() {
				o := v.(producers.MetricsMessage)

				age := time.Now().Sub(time.Unix(o.Timestamp, 0))
				if age > p.config.CacheExpiry {
					httpLog.Debugf("Removing stale object %s; last updated %d seconds ago", k, age/time.Second)
					p.store.Delete(k)
				}
			}
		}
	}
}
