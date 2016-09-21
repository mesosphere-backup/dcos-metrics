package main

import (
	"fmt"
	"log"
	"os"

	"github.com/mesosphere/dcos-stats/collector"
)

func main() {
	collectorConfig, err := parseArgsReturnConfig(os.Args)
	if err != nil {
		fmt.Println(err.Error())
		os.Exit(1)
	}

	stats := make(chan collector.StatsEvent)
	go collector.RunStatsEmitter(stats)

	kafkaOutputChan := make(chan collector.KafkaMessage)
	if collectorConfig.KafkaFlagEnabled {
		go collector.RunKafkaProducer(kafkaOutputChan, stats)
	} else {
		go printReceivedMessages(kafkaOutputChan)
	}

	recordInputChan := make(chan *collector.AvroDatum)
	agentStateChan := make(chan *collector.AgentState)
	if collectorConfig.PollAgentEnabled {
		go collector.RunAgentPoller(recordInputChan, agentStateChan, stats)
	}
	go collector.RunAvroTCPReader(recordInputChan, stats)

	if collectorConfig.HttpProfilerEnabled {
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
