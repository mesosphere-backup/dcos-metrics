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
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/store"
	"github.com/dcos/dcos-metrics/producers"
)

// Config for the HTTP producer
type Config struct {
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
	r := newRouter(p)
	if err := http.ListenAndServe(fmt.Sprintf(":%d", p.config.Port), r); err != nil {
		log.Fatalf("error: http producer: %s", err)
	}
	log.Infof("The HTTP producer is serving requests on port %d", p.config.Port)
	log.Info("Starting janitor for in-memory data store")
	go p.janitor()

	for {
		message := <-p.metricsChan         // read messages off the channel
		p.store.Set(message.Name, message) // overwrite existing object with the same message.Name
	}
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

				lastUpdated := time.Since(o.Timestamp)
				if lastUpdated > p.config.CacheExpiry {
					log.Debugf("Removing stale object %s; last updated %d seconds ago", o.Name, lastUpdated*time.Second)
					p.store.Delete(o.Name)
				}
			}
		}
	}
}
