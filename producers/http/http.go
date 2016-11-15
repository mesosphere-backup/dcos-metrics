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
	"net/http"
	"strings"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/coreos/go-systemd/activation"
	"github.com/dcos/dcos-go/store"
	"github.com/dcos/dcos-metrics/producers"
)

// Config for the HTTP producer
type Config struct {
	IP          string        `yaml:"ip"`
	Port        int           `yaml:"port"`
	CacheExpiry time.Duration // ideally this is a multiple of the collector's PollingPeriod
}

type producerImpl struct {
	config      Config
	store       store.Store
	metricsChan chan producers.MetricsMessage
}

// New creates a new instance of the HTTP producer with the provided configuration.
func New(cfg Config) (producers.MetricsProducer, chan producers.MetricsMessage) {
	p := producerImpl{
		config:      cfg,
		store:       store.New(),
		metricsChan: make(chan producers.MetricsMessage),
	}
	return &p, p.metricsChan
}

// Run a HTTP server and serve the various metrics API endpoints.
// This function should be run in its own goroutine.
func (p *producerImpl) Run() error {
	log.Info("Starting HTTP producer garbage collection service")
	go p.janitor()

	go func() {
		log.Debug("HTTP producer listening for incoming messages on metricsChan")
		for {
			// read messages off the channel,
			// and give them a unique name in the store
			message := <-p.metricsChan
			log.Debugf("Received message '%+v' with timestamp %s",
				message, time.Unix(message.Timestamp, 0).Format(time.RFC3339))

			var name string
			switch message.Name {
			case producers.AgentMetricPrefix:
				name = strings.Join([]string{
					message.Name,
					message.Dimensions.AgentID,
				}, producers.MetricNamespaceSep)
			case producers.ContainerMetricPrefix:
				name = strings.Join([]string{
					message.Name,
					message.Dimensions.ContainerID,
				}, producers.MetricNamespaceSep)
			case producers.AppMetricPrefix:
				name = strings.Join([]string{
					message.Name,
					message.Dimensions.ContainerID,
				}, producers.MetricNamespaceSep)
			}
			log.Debugf("Setting store object '%s' with timestamp %s",
				name, time.Unix(message.Timestamp, 0).Format(time.RFC3339))
			p.store.Set(name, message) // overwrite existing object with the same name
		}
	}()

	r := newRouter(p)
	listeners, err := activation.Listeners(true)
	if err != nil {
		return fmt.Errorf("Unable to get listeners: %s", err)
	}
	// If a listener is avialable, use that. If it is not avialable,
	// listen on the default TCP socket and port.
	if len(listeners) == 1 {
		log.Infof("HTTP Producer serving requests on %s", listeners[0].Addr().String())
		return http.Serve(listeners[0], r)
	}
	log.Infof("HTTP Producer serving requests on %s:%d", p.config.IP, p.config.Port)
	return http.ListenAndServe(fmt.Sprintf("%s:%d", p.config.IP, p.config.Port), r)
}

// janitor analyzes the objects in the store and removes stale objects. An
// object is considered stale when the top-level timestamp of its MetricsMessage
// has exceeded the CacheExpiry, which is calculated as a multiple of the
// collector's polling period. This function should be run in its own goroutine.
func (p *producerImpl) janitor() {
	ticker := time.NewTicker(time.Duration(60 * time.Second))
	for {
		select {
		case _ = <-ticker.C:
			for _, obj := range p.store.Objects() {
				o := obj.(producers.MetricsMessage)

				age := time.Since(time.Unix(o.Timestamp, 0))
				if age > p.config.CacheExpiry {
					log.Debugf("Removing stale object %s; last updated %d seconds ago", o.Name, age*time.Second)
					p.store.Delete(o.Name)
				}
			}
		}
	}
}
