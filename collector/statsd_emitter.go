package collector

import (
	"fmt"
	"log"
	"net"
	"strconv"
	"time"
)

var (
	statsdHostFlag = StringEnvFlag("statsd-udp-host", "",
		"Outgoing host for sending statsd metrics")
	statsdPortFlag = IntEnvFlag("statsd-udp-port", 0,
		"Outgoing UDP port for sending statsd metrics")
	statsdPeriodFlag = IntEnvFlag("statsd-period", 15,
		"Period between statsd metrics flushes, in seconds")
)

type StatsEventType int

const (
	udpFrameSize = 512

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
	KafkaBytesSent

	AvroWriterFailed
	AvroRecordOut

	AgentIpLookup
	AgentIpLookupFailed
	AgentIpLookupEmpty
	AgentQuery
	AgentQueryBadMetrics
	AgentQueryBadState
	AgentQueryFailed
	AgentMetricsValue
	AgentMetricsValueUnsupported

	RecordNoAgentStateAvailable
	RecordBadTopic
	RecordBadTags
)

type StatsEvent struct {
	evttype StatsEventType
	suffix  string
	count   int
}

func MakeEventSuffCount(evttype StatsEventType, suffix string, count int) StatsEvent {
	return StatsEvent{evttype, suffix, count}
}
func MakeEventCount(evttype StatsEventType, count int) StatsEvent {
	return StatsEvent{evttype, "", count}
}
func MakeEventSuff(evttype StatsEventType, suffix string) StatsEvent {
	return StatsEvent{evttype, suffix, 1}
}
func MakeEvent(evttype StatsEventType) StatsEvent {
	return StatsEvent{evttype, "", 1}
}

// Creates and runs a Stats Emitter which sends counts to a UDP endpoint,
// or which just emits to logs if the endpoint isn't available.
// This function should be run as a gofunc.
func RunStatsEmitter(events <-chan StatsEvent) {
	gauges := make(map[string]int64)
	statsdConn := getStatsdConn()
	ticker := time.NewTicker(time.Second * time.Duration(*statsdPeriodFlag))
	for {
		select {
		case event := <-events:
			gauges[toStatsdLabel(event)]++
			if len(event.suffix) != 0 {
				// also count against non-suffix bucket:
				gauges[toStatsdLabel(StatsEvent{event.evttype, "", event.count})]++
			}
		case _ = <-ticker.C:
			flushGauges(statsdConn, &gauges)
		}
	}
}

// ---

func toStatsdLabel(event StatsEvent) string {
	typelabel := "UNKNOWN"
	switch event.evttype {
	case TCPResolveFailed:
		typelabel = "tcp_input.tcp_resolve_failures"
	case TCPListenFailed:
		typelabel = "tcp_input.tcp_listen_failures"
	case TCPAcceptFailed:
		typelabel = "tcp_input.tcp_accept_failures"
	case TCPSessionOpened:
		typelabel = "tcp_input.tcp_session_opened"
	case TCPSessionClosed:
		typelabel = "tcp_input.tcp_session_closed"
	case AvroReaderOpenFailed:
		typelabel = "tcp_input.avro_reader_open_failures"
	case AvroReaderCloseFailed:
		typelabel = "tcp_input.avro_reader_close_failures"
	case AvroRecordIn:
		typelabel = "tcp_input.avro_record"

	case KafkaLookupFailed:
		typelabel = "kafka_output.framework_lookup_failures"
	case KafkaConnectionFailed:
		typelabel = "kafka_output.connection_failures"
	case KafkaSessionOpened:
		typelabel = "kafka_output.sessions_opened"
	case KafkaSessionClosed:
		typelabel = "kafka_output.sessions_closed"
	case KafkaMessageSent:
		typelabel = "kafka_output.avro_records_sent"
	case KafkaBytesSent:
		typelabel = "kafka_output.bytes_sent"

	case AvroWriterFailed:
		typelabel = "avro_output.writer_open_failures"
	case AvroRecordOut:
		typelabel = "avro_output.avro_record"

	case AgentIpLookup:
		typelabel = "agent_poll.ip_lookups"
	case AgentIpLookupFailed:
		typelabel = "agent_poll.ip_lookup_failures"
	case AgentIpLookupEmpty:
		typelabel = "agent_poll.ip_lookup_empties"
	case AgentQuery:
		typelabel = "agent_poll.queries"
	case AgentQueryBadMetrics:
		typelabel = "agent_poll.query_bad_metrics"
	case AgentQueryBadState:
		typelabel = "agent_poll.query_bad_state"
	case AgentQueryFailed:
		typelabel = "agent_poll.query_failures"
	case AgentMetricsValue:
		typelabel = "agent_poll.metrics_values"
	case AgentMetricsValueUnsupported:
		typelabel = "agent_poll.metrics_values_unsupported"

	case RecordNoAgentStateAvailable:
		typelabel = "topic_sorter.no_agent_state_available"
	case RecordBadTopic:
		typelabel = "topic_sorter.record_bad_topic"
	case RecordBadTags:
		typelabel = "topic_sorter.record_bad_tags"
	}
	if len(event.suffix) == 0 {
		return fmt.Sprintf("collector.%s", typelabel)
	} else {
		return fmt.Sprintf("collector.%s.%s", typelabel, event.suffix)
	}
}

func getStatsdConn() *net.UDPConn {
	if statsdHostFlag == nil || len(*statsdHostFlag) == 0 ||
		statsdPortFlag == nil || *statsdPortFlag == 0 {
		log.Println("STATSD_UDP_HOST and/or STATSD_UDP_PORT not present in environment. " +
			"Internal collector metrics over StatsD is disabled.")
		return nil
	}
	address := fmt.Sprintf("%s:%s", *statsdHostFlag, strconv.Itoa(int(*statsdPortFlag)))
	// send the current time (gauge) and the count of packets sent (gauge)
	dest, err := net.ResolveUDPAddr("udp", address)
	if err != nil {
		log.Fatalf("Unable to resolve provided StatsD endpoint (%s): %s", address, err)
	}
	conn, err := net.DialUDP("udp", nil, dest)
	if err != nil {
		log.Fatalf("Unable to dial provided StatsD endpoint (%s/%s): %s", address, dest, err)
	}
	log.Println("Opened connection to StatsD endpoint:", address)
	return conn
}

func flushGauges(conn *net.UDPConn, gauges *map[string]int64) {
	var keysToClear []string

	if conn == nil {
		// Statsd export isn't available. Just print stats to stdout.
		log.Printf("Printing %d internal gauges (StatsD export disabled):\n", len(*gauges))
		for k, v := range *gauges {
			log.Printf("- %s = %s\n", k, strconv.FormatInt(v, 10))
			keysToClear = append(keysToClear, k)
		}
	} else {
		// accumulate statsd data into a newline-separated block, staying within UDP packet limits
		msgBlock := ""
		log.Printf("Sending %d internal gauges to StatsD endpoint %s:\n", len(*gauges), conn.RemoteAddr())
		for k, v := range *gauges {
			log.Printf("- %s = %s\n", k, strconv.FormatInt(v, 10))
			nextMsg := fmt.Sprintf("%s:%s|g", k, strconv.FormatInt(v, 10))
			if len(msgBlock)+len(nextMsg)+1 > udpFrameSize {
				// would exceed udp max. flush msgBlock and populate with nextMsg
				if len(msgBlock) != 0 {
					_, err := conn.Write([]byte(msgBlock))
					if err != nil {
						log.Printf("Failed to send %d bytes to StatsD endpoint %s: %s\n",
							len(msgBlock), conn.RemoteAddr(), err)
					}
				}
				msgBlock = nextMsg
			} else {
				// append nextMsg to msg
				if len(msgBlock) == 0 {
					msgBlock = nextMsg
				} else {
					msgBlock += "\n" + nextMsg
				}
			}
			keysToClear = append(keysToClear, k)
		}
		if len(msgBlock) != 0 {
			// flush any remainder
			_, err := conn.Write([]byte(msgBlock))
			if err != nil {
				log.Printf("Failed to send %d bytes to StatsD endpoint %s: %s\n",
					len(msgBlock), conn.RemoteAddr(), err)
			}
		}
	}

	// set all defined gauge values to zero
	for _, k := range keysToClear {
		(*gauges)[k] = 0
	}
}
