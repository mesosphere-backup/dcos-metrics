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
	"log"
	"os"

	"github.com/dcos/dcos-metrics/collector"
)

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
