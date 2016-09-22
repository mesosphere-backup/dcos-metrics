package main

import (
	"fmt"
	"log"
	"os"

	"github.com/mesosphere/dcos-metrics/collector"
)

func main() {
	collectorConfig, err := parseArgsReturnConfig(os.Args[1:])
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
	if collectorConfig.PollAgent {
		log.Printf("Agent polling enabled")
		go collector.RunAgentPoller(recordInputChan, agentStateChan, stats)
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
