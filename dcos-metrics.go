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
	"os"
	"reflect"
	"strings"

	log "github.com/Sirupsen/logrus"

	frameworkCollector "github.com/dcos/dcos-metrics/collectors/framework"
	mesosAgentCollector "github.com/dcos/dcos-metrics/collectors/mesos/agent"
	nodeCollector "github.com/dcos/dcos-metrics/collectors/node"
	"github.com/dcos/dcos-metrics/producers"
	httpProducer "github.com/dcos/dcos-metrics/producers/http"
	"github.com/dcos/dcos-metrics/util/http/profiler"
)

func main() {
	// Get configuration
	cfg, err := getNewConfig(os.Args[1:])
	if err != nil {
		log.Fatal(err)
		os.Exit(1)
	}

	// Set logging level
	lvl, err := log.ParseLevel(cfg.LogLevel)
	if err != nil {
		log.Fatal(err)
		os.Exit(1)
	}
	log.SetLevel(lvl)

	//AuthConfig
	if len(cfg.Collector.MesosAgent.Principal) > 0 {
		log.Info("Framework authentication set to principal", cfg.Collector.MesosAgent.Principal)
	}

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

		hp, httpProducerChan := httpProducer.New(
			cfg.Producers.HTTPProducerConfig)
		producerChans = append(producerChans, httpProducerChan)
		go hp.Run()
	}

	// Initialize and run the node poller
	node, nodeChan := nodeCollector.New(*cfg.Collector.Node, cfg.nodeInfo)
	go node.RunPoller()

	// Create a ContainerTaskRels object for collectors to rely on
	containerTaskRels := mesosAgentCollector.NewContainerTaskRels()

	// Initialize and run the StatsD collector and the Mesos agent poller
	mesosAgent, mesosAgentChan := mesosAgentCollector.New(*cfg.Collector.MesosAgent, cfg.nodeInfo, containerTaskRels)
	framework, frameworkChan := frameworkCollector.New(*cfg.Collector.Framework, cfg.nodeInfo, containerTaskRels)

	if cfg.DCOSRole == "agent" {
		go framework.RunFrameworkTCPListener()
		go mesosAgent.RunPoller()
	}

	// Broadcast (many-to-many) messages from the collector to the various producers.
	for {
		select {
		case metric := <-frameworkChan:
			broadcast(metric, producerChans)
		case metric := <-nodeChan:
			broadcast(metric, producerChans)
		case metric := <-mesosAgentChan:
			broadcast(metric, producerChans)
		}
	}
}

// broadcast sends a MetricsMessage to a range of producer chans
func broadcast(msg producers.MetricsMessage, producers []chan<- producers.MetricsMessage) {
	for _, producer := range producers {
		producer <- msg
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
