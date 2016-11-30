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

package main

import (
	"fmt"
	"os"
	"reflect"
	"strings"
	"time"

	log "github.com/Sirupsen/logrus"

	frameworkCollector "github.com/dcos/dcos-metrics/collectors/framework"
	"github.com/dcos/dcos-metrics/producers"
	httpProducer "github.com/dcos/dcos-metrics/producers/http"
	"github.com/dcos/dcos-metrics/util/http/profiler"
	//kafkaProducer "github.com/dcos/dcos-metrics/producers/kafka"
	//statsdProducer "github.com/dcos/dcos-metrics/producers/statsd"
)

func main() {
	// Get configuration
	cfg, err := getNewConfig(os.Args[1:])
	if err != nil {
		log.Fatal(err)
		os.Exit(1)
	}
	if cfg.VersionFlag {
		fmt.Printf("DC/OS Metrics Service\nVersion: %s\nRevision: %s\n\r", VERSION, REVISION)
		os.Exit(0)
	}

	// Set logging level
	lvl, err := log.ParseLevel(cfg.LogLevel)
	if err != nil {
		log.Fatal(err)
		os.Exit(1)
	}
	log.SetLevel(lvl)

	// HTTP profiling
	if cfg.Collector.HTTPProfiler {
		log.Info("HTTP profiling enabled")
		go profiler.RunHTTPProfAccess()
	}

	var producerChans []chan<- producers.MetricsMessage

	// HTTP producer
	if producerIsConfigured("http", cfg) {
		log.Info("HTTP producer enabled")
		cfg.Producers.HTTPProducerConfig.DCOSRole = cfg.DCOSRole
		cfg.Producers.HTTPProducerConfig.CacheExpiry = time.Duration(cfg.Collector.MesosAgent.PollPeriod) * time.Minute * 2

		hp, httpProducerChan := httpProducer.New(
			cfg.Producers.HTTPProducerConfig)
		producerChans = append(producerChans, httpProducerChan)
		go hp.Run()
	}

	// Initialize and run the host-poller
	cfg.Collector.Node.MetricsChan = make(chan producers.MetricsMessage)
	go cfg.Collector.Node.RunPoller()

	// Initialize agent specific channels and run agent specific pollers
	// if role is of type agent
	frameworkCollectorChan := make(chan *frameworkCollector.AvroDatum)
	framework := frameworkCollector.New()
	cfg.Collector.MesosAgent.MetricsChan = make(chan producers.MetricsMessage)
	if cfg.DCOSRole == "agent" {
		go framework.RunFrameworkTCPListener(frameworkCollectorChan)
		go cfg.Collector.MesosAgent.RunPoller()
	}

	// Broadcast (many-to-many) messages from the collector to the various producers.
	for {
		select {

		case frameworkMessage := <-frameworkCollectorChan:
			pmm, err := frameworkMessage.Transform(cfg.MesosID, cfg.ClusterID, cfg.IPAddress)
			if err != nil {
				log.Error(err)
			}
			for _, producer := range producerChans {
				producer <- pmm
			}

		case nodeCollectorMetric := <-cfg.Collector.Node.MetricsChan:
			for _, producer := range producerChans {
				producer <- nodeCollectorMetric
			}

		case mesosAgentCollectorMetric := <-cfg.Collector.MesosAgent.MetricsChan:
			for _, producer := range producerChans {
				producer <- mesosAgentCollectorMetric
			}
		}
	}
}

// producerIsConfigured analyzes the ProducersConfig struct and determines if
// configuration exists for a given producer by name (i.e., is the "http"
// producer configured?). If a configuration exists, this function will return
// true, as a configured producer is an enabled one.
func producerIsConfigured(name string, cfg Config) bool {
	s := reflect.ValueOf(cfg.Producers)
	cfgType := s.Type()
	for i := 0; i < s.NumField(); i++ {
		if strings.Split(cfgType.Field(i).Tag.Get("yaml"), ",")[0] == name {
			return true
		}
	}
	return false
}
