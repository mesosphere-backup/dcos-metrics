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

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/collector/collector"
	yaml "gopkg.in/yaml.v2"
)

type MasterConfig struct {
	Port  int    `yaml:"port,omitempty"`
	Topic string `yaml:"metric_topic,omitempty"`
}

type AgentConfig struct {
	Port  int    `yaml:"port,omitempty"`
	Topic string `yaml:"metric_topic,omitempty"`
}

type CollectorConfig struct {
	HttpProfiler  bool   `yaml:"http_profiler"`
	KafkaProducer bool   `yaml:"kafka_producer"`
	IpCommand     string `yaml:"ip_command"`
	PollingPeriod int    `yaml:"polling_period"`

	AgentConfig  AgentConfig  `yaml:"agent_config,omitempty"`
	MasterConfig MasterConfig `yaml:"master_config,omitempty"`

	ConfigPath string
	DCOSRole   string
}

func main() {
	collectorConfig, err := getNewConfig(os.Args[1:])
	if err != nil {
		fmt.Println(err.Error())
		os.Exit(1)
	}

	stats := make(chan collector.StatsEvent)
	go collector.RunStatsEmitter(stats)

	kafkaOutputChan := make(chan collector.KafkaMessage)
	if collectorConfig.KafkaProducer {
		log.Printf("Kafkfa producer enabled")
		go collector.RunKafkaProducer(kafkaOutputChan, stats)
	} else {
		go printReceivedMessages(kafkaOutputChan)
	}

	recordInputChan := make(chan *collector.AvroDatum)
	agentStateChan := make(chan *collector.AgentState)
	if collectorConfig.DCOSRole == "agent" {
		log.Printf("Agent polling enabled")
		agent, err := collector.NewAgent(
			collectorConfig.IpCommand,
			collectorConfig.AgentConfig.Port,
			collectorConfig.PollingPeriod,
			collectorConfig.AgentConfig.Topic)
		if err != nil {
			log.Fatal(err.Error())
		}

		go agent.Run(recordInputChan, stats)
	}
	go collector.RunAvroTCPReader(recordInputChan, stats)

	if collectorConfig.HttpProfiler {
		log.Printf("HTTP Profiling Enabled")
		go collector.RunHTTPProfAccess()
	}

	// Run the sorter on the main thread (exit process if Kafka stops accepting data)
	collector.RunTopicSorter(recordInputChan, agentStateChan, kafkaOutputChan, stats)
}

func printReceivedMessages(msgChan <-chan collector.KafkaMessage) {
	for {
		msg := <-msgChan
		log.Printf("Topic '%s': %d bytes would've been written (-kafka=false)\n",
			msg.Topic, len(msg.Data))
	}
}

func (c *CollectorConfig) setFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.ConfigPath, "config", c.ConfigPath, "The path to the config file.")
	fs.StringVar(&c.DCOSRole, "role", c.DCOSRole, "The DC/OS role this instance runs on.")
}

func (c *CollectorConfig) loadConfig() error {
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

func newConfig() CollectorConfig {
	return CollectorConfig{
		HttpProfiler:  true,
		KafkaProducer: true,
		PollingPeriod: 15,
		IpCommand:     "/opt/mesosphere/bin/detect_ip",
		ConfigPath:    "dcos-metrics-config.yaml",
	}
}

func getNewConfig(args []string) (CollectorConfig, error) {
	c := newConfig()
	thisFlagSet := flag.NewFlagSet("", flag.PanicOnError)
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
