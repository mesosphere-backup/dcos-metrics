package main

import (
	"flag"
	"fmt"
	"log"
	"os"

	"github.com/mesosphere/dcos-stats/collector"
)

var (
	agentPollingFlag = collector.BoolEnvFlag("agent-polling", true,
		"Polls the local Mesos Agent for system information")
	httpProfileFlag = collector.BoolEnvFlag("http-profile", false,
		"Allows access to profile data on a random HTTP port at /debug/pprof")
	kafkaFlag = collector.BoolEnvFlag("kafka", true,
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
	if *kafkaFlag {
		go collector.RunKafkaProducer(kafkaOutputChan, stats)
	} else {
		go printReceivedMessages(kafkaOutputChan)
	}

	recordInputChan := make(chan *collector.AvroDatum)
	agentStateChan := make(chan *collector.AgentState)
	if *agentPollingFlag {
		go collector.RunAgentPoller(recordInputChan, agentStateChan, stats)
	}
	go collector.RunAvroTCPReader(recordInputChan, stats)

	if *httpProfileFlag {
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
