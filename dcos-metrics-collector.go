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
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"reflect"
	"strings"

	log "github.com/Sirupsen/logrus"
	yaml "gopkg.in/yaml.v2"

	"github.com/dcos/dcos-metrics/collector"
	httpProducer "github.com/dcos/dcos-metrics/producers/http"
	kafkaProducer "github.com/dcos/dcos-metrics/producers/kafka"
	statsdProducer "github.com/dcos/dcos-metrics/producers/statsd"
	"github.com/dcos/dcos-metrics/util"
)

// Config defines the top-level configuration options for the dcos-metrics-collector project.
// It is (currently) broken up into two main sections: collectors and producers.
type Config struct {
	Collector CollectorConfig `yaml:"collector"`
	Producers ProducersConfig `yaml:"producers"`

	ConfigPath string
	DCOSRole   string
}

// CollectorConfig contains configuration options relevant to the "collector"
// portion of this project. That is, the code responsible for querying Mesos,
// et. al to gather metrics and send them to a "producer".
type CollectorConfig struct {
	HTTPProfiler  bool   `yaml:"http_profiler"`
	IPCommand     string `yaml:"ip_command"`
	PollingPeriod int    `yaml:"polling_period"`

	MasterConfig MasterConfig `yaml:"master_config,omitempty"`
	AgentConfig  AgentConfig  `yaml:"agent_config,omitempty"`
}

// ProducersConfig contains references to other structs that provide individual producer configs.
// The configuration for all producers is then located in their corresponding packages.
//
// For example: Config.Producers.KafkaProducerConfig references kafkaProducer.Config. This struct
// contains an optional Kafka configuration. This configuration is available in the source file
// 'producers/kafka/kafka.go'. It is then the responsibility of the individual producers to
// validate the configuration the user has provided and panic if necessary.
type ProducersConfig struct {
	HTTPProducerConfig   httpProducer.Config   `yaml:"http,omitempty"`
	KafkaProducerConfig  kafkaProducer.Config  `yaml:"kafka,omitempty"`
	StatsdProducerConfig statsdProducer.Config `yaml:"statsd,omitempty"`
}

// MasterConfig contains configuration options relevant to metrics collection
// from a DC/OS (Mesos) master.
type MasterConfig struct {
	Port       int    `yaml:"port,omitempty"`
	KafkaTopic string `yaml:"kafka_topic,omitempty"`
}

// AgentConfig contains configuration options relevant to metrics collection
// from a DC/OS (Mesos) agent.
type AgentConfig struct {
	Port       int    `yaml:"port,omitempty"`
	KafkaTopic string `yaml:"kafka_topic,omitempty"`
}

func main() {
	cfg, err := getNewConfig(os.Args[1:])
	if err != nil {
		fmt.Println(err.Error())
		os.Exit(1)
	}

	// HTTP profiling
	if cfg.Collector.HTTPProfiler {
		log.Printf("HTTP Profiling Enabled")
		go util.RunHTTPProfAccess()
	}

	// HTTP producer
	if producerIsConfigured("http", cfg) {
		go httpProducer.Start(cfg.Producers.HTTPProducerConfig)
	}

	// StatsD producer
	stats := make(chan statsdProducer.StatsEvent)
	if producerIsConfigured("statsd", cfg) {
		go statsdProducer.RunStatsEmitter(stats, cfg.Producers.StatsdProducerConfig)
	}

	// Kafka producer
	kafkaOutputChan := make(chan kafkaProducer.KafkaMessage)
	if producerIsConfigured("kafka", cfg) {
		log.Printf("Kafka producer enabled")
		go kafkaProducer.RunKafkaProducer(kafkaOutputChan, stats, cfg.Producers.KafkaProducerConfig)
	} else {
		go printReceivedMessages(kafkaOutputChan)
	}

	recordInputChan := make(chan *collector.AvroDatum)
	agentStateChan := make(chan *collector.AgentState)
	if cfg.DCOSRole == "agent" {
		log.Printf("Agent polling enabled")
		agent, err := collector.NewAgent(
			cfg.Collector.IPCommand,
			cfg.Collector.AgentConfig.Port,
			cfg.Collector.PollingPeriod,
			cfg.Collector.AgentConfig.KafkaTopic)
		if err != nil {
			log.Fatal(err.Error())
		}

		go agent.Run(recordInputChan, stats)
	}
	go collector.RunAvroTCPReader(recordInputChan, stats)

	// Run the sorter on the main thread (exit process if Kafka stops accepting data)
	collector.RunTopicSorter(recordInputChan, agentStateChan, kafkaOutputChan, stats)
}

func printReceivedMessages(msgChan <-chan kafkaProducer.KafkaMessage) {
	for {
		msg := <-msgChan
		log.Printf("Topic '%s': %d bytes would've been written (-kafka=false)\n",
			msg.Topic, len(msg.Data))
	}
}

func (c *Config) setFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.ConfigPath, "config", c.ConfigPath, "The path to the config file.")
	fs.StringVar(&c.DCOSRole, "role", c.DCOSRole, "The DC/OS role this instance runs on.")
}

func (c *Config) loadConfig() error {
	fmt.Printf("Loading config file from %s\n", c.ConfigPath)
	fileByte, err := ioutil.ReadFile(c.ConfigPath)
	if err != nil {
		return err
	}

	if err = yaml.Unmarshal(fileByte, &c); err != nil {
		return err
	}

	return nil
}

// newConfig establishes our default, base configuration.
func newConfig() Config {
	return Config{
		Collector: CollectorConfig{
			HTTPProfiler:  true,
			IPCommand:     "/opt/mesosphere/bin/detect_ip",
			PollingPeriod: 15,
			MasterConfig:  MasterConfig{Port: 5050},
			AgentConfig:   AgentConfig{Port: 5051},
		},
		Producers: ProducersConfig{
			HTTPProducerConfig: httpProducer.Config{Port: 8000},
		},
		ConfigPath: "dcos-metrics-config.yaml",
	}
}

// getNewConfig loads the configuration and sets precedence of configuration values.
// For example: command line flags override values provided in the config file.
func getNewConfig(args []string) (Config, error) {
	c := newConfig()
	thisFlagSet := flag.NewFlagSet("", flag.ExitOnError)
	c.setFlags(thisFlagSet)
	// Override default config with CLI flags if any
	if err := thisFlagSet.Parse(args); err != nil {
		fmt.Println("Errors encountered parsing flags.")
		return c, err
	}

	if err := c.loadConfig(); err != nil {
		return c, err
	}

	return c, nil
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
