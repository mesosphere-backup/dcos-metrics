package collector

import (
	"log"
)

var (
	statsdHostFlag = StringEnvFlag("statsd-udp-host", "",
		"Outgoing host for sending statsd metrics")
	statsdPortFlag = IntEnvFlag("statsd-udp-port", 0,
		"Outgoing UDP port for sending statsd metrics")
)

type StatsEventType int

const (
	TCPResolveFailed StatsEventType = iota
	TCPListenFailed
	TCPAcceptFailed
	TCPSessionOpened
	TCPSessionClosed
	AvroReaderOpenFailed
	AvroReaderCloseFailed
	AvroRecordIn

	KafkaLookupFailed
	KafkaConnectionFailed
	KafkaSessionOpened
	KafkaSessionClosed
	KafkaMessageSent

	FileOutputFailed
	FileOutputWritten

	AvroWriterOpenFailed
	AvroRecordOut
	AvroWriterCloseFailed

	// sample-producer only:
	AgentIPFailed
	AgentQueryFailed
	AgentQueryEmpty
	AgentQueried
)

type StatsEvent struct {
	Type   StatsEventType
	Suffix string
}

func StartStatsLoop(events <-chan StatsEvent) {
	//TODO launch gofunc that emits counters (which all explicitly start at zero)
	for event := range events {
		//TODO update counters (both with suffix and without), send statsd
		log.Printf("GOT EVENT: %+v\n", event)
	}
}
