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

	"github.com/dcos/dcos-metrics/producers"
)

// Config for the HTTP producer
type Config struct {
	Port int `yaml:"port"`
}

type producerImpl struct {
	config      Config
	metricsChan chan<- producers.MetricsMessage
}

// New creates a new instance of the HTTP producer with the provided configuration.
func New(cfg interface{}) (producers.ProducerInterface, chan<- producers.MetricsMessage) {
	var p producerImpl
	p.config = cfg.(Config)
	p.metricsChan = make(chan producers.MetricsMessage)
	return &p, p.metricsChan
}

// Run a HTTP server and serve the various metrics API endpoints.
// This function should be run in its own goroutine.
func (p *producerImpl) Run() error {
	r := newRouter()
	if err := http.ListenAndServe(fmt.Sprintf(":%d", p.config.Port), r); err != nil {
		return err
	}
	return nil

}
