package main

import (
	"flag"
	"fmt"
	"github.com/mesosphere/dcos-stats/collector"
	"log"
	"os"
)

var (
	agentPollingEnabledFlag = collector.BoolEnvFlag("agent-polling-enabled", true,
		"Polls the local Mesos Agent for system information")
	kafkaEnabledFlag = collector.BoolEnvFlag("kafka-enabled", true,
		"Sends output data to Kafka")
)

func main() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr,
			"Collects and forwards data in Metrics Avro format to the provided Kafka service\n")
		fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()
	flag.VisitAll(collector.PrintFlagEnv)

	stats := make(chan collector.StatsEvent)
	go collector.RunStatsEmitter(stats)

	kafkaOutputChan := make(chan collector.KafkaMessage)
	if *kafkaEnabledFlag {
		go collector.RunKafkaProducer(kafkaOutputChan, stats)
	} else {
		go printReceivedMessages(kafkaOutputChan)
	}

	recordInputChan := make(chan interface{})
	agentStateChan := make(chan *collector.AgentState)
	if *agentPollingEnabledFlag {
		go collector.RunAgentPoller(recordInputChan, agentStateChan, stats)
	}
	go collector.RunAvroTCPReader(recordInputChan, stats)

	// Run the sorter on the main thread (exit process if Kafka stops accepting data)
	collector.RunTopicSorter(recordInputChan, agentStateChan, kafkaOutputChan, stats)
}

func printReceivedMessages(msgChan <-chan collector.KafkaMessage) {
	for {
		msg := <-msgChan
		log.Printf("Topic '%s': %d bytes would've been written (-kafka-enabled=false)\n",
			msg.Topic, len(msg.Data))
	}
}
