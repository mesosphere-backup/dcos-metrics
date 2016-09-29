package collector

import (
	"fmt"
	"log"
	"net"
	"sort"
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
	statsdPrefix = "dcos.metrics.collector"
	udpFrameSize = 512

	TCPResolveFailed StatsEventType = iota
	TCPListenFailed
	TCPAcceptFailed
	TCPSessionOpened
	TCPSessionClosed
	AvroReaderOpenFailed
	AvroReaderCloseFailed
	AvroRecordIn
	AvroRecordInThrottled
	AvroBytesIn
	AvroBytesInThrottled

	KafkaLookupFailed
	KafkaConnectionFailed
	KafkaSessionOpened
	KafkaSessionClosed
	KafkaMessageSent
	KafkaBytesSent

	AgentIpLookup
	AgentIpLookupFailed
	AgentIpLookupEmpty
	AgentQuery
	AgentQueryBadData
	AgentQueryFailed
	AgentMetricsValue
	AgentMetricsValueUnsupported

	RecordNoAgentStateAvailable
	RecordBadTopic
	RecordBadTags
	AvroWriterFailed
	AvroRecordOut
	AvroRecordOutThrottled
	AvroBytesOut
	AvroBytesOutThrottled
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
			gauges[toStatsdLabel(event)] += int64(event.count)
			if len(event.suffix) != 0 {
				// also count against non-suffix bucket:
				gauges[toStatsdLabel(StatsEvent{event.evttype, "", event.count})] += int64(event.count)
			}
		case _ = <-ticker.C:
			flushGauges(statsdConn, &gauges)
		}
	}
}

// ---

func toStatsdLabel(event StatsEvent) string {
	statsdKey := "UNKNOWN"
	switch event.evttype {
	case TCPResolveFailed:
		statsdKey = "tcp_input.tcp_resolve_failures"
	case TCPListenFailed:
		statsdKey = "tcp_input.tcp_listen_failures"
	case TCPAcceptFailed:
		statsdKey = "tcp_input.tcp_accept_failures"
	case TCPSessionOpened:
		statsdKey = "tcp_input.tcp_sessions_opened"
	case TCPSessionClosed:
		statsdKey = "tcp_input.tcp_sessions_closed"
	case AvroReaderOpenFailed:
		statsdKey = "tcp_input.avro_reader_open_failures"
	case AvroReaderCloseFailed:
		statsdKey = "tcp_input.avro_reader_close_failures"
	case AvroRecordIn:
		statsdKey = "tcp_input.records"
	case AvroRecordInThrottled:
		statsdKey = "tcp_input.records_throttled"
	case AvroBytesIn:
		statsdKey = "tcp_input.bytes"
	case AvroBytesInThrottled:
		statsdKey = "tcp_input.bytes_throttled"

	case KafkaLookupFailed:
		statsdKey = "kafka_output.framework_lookup_failures"
	case KafkaConnectionFailed:
		statsdKey = "kafka_output.connection_failures"
	case KafkaSessionOpened:
		statsdKey = "kafka_output.sessions_opened"
	case KafkaSessionClosed:
		statsdKey = "kafka_output.sessions_closed"
	case KafkaMessageSent:
		statsdKey = "kafka_output.avro_records_sent"
	case KafkaBytesSent:
		statsdKey = "kafka_output.bytes_sent"

	case AgentIpLookup:
		statsdKey = "agent_poll.ip_lookups"
	case AgentIpLookupFailed:
		statsdKey = "agent_poll.ip_lookup_failures"
	case AgentIpLookupEmpty:
		statsdKey = "agent_poll.ip_lookup_empties"
	case AgentQuery:
		statsdKey = "agent_poll.queries"
	case AgentQueryBadData:
		statsdKey = "agent_poll.query_bad_data"
	case AgentQueryFailed:
		statsdKey = "agent_poll.query_failures"
	case AgentMetricsValue:
		statsdKey = "agent_poll.metrics_values"
	case AgentMetricsValueUnsupported:
		statsdKey = "agent_poll.metrics_values_unsupported"

	case RecordNoAgentStateAvailable:
		statsdKey = "topic_sorter.no_agent_state_available"
	case RecordBadTopic:
		statsdKey = "topic_sorter.records_bad_topic"
	case RecordBadTags:
		statsdKey = "topic_sorter.records_bad_tags"
	case AvroWriterFailed:
		statsdKey = "topic_sorter.avro_writer_open_failures"
	case AvroRecordOut:
		statsdKey = "topic_sorter.records"
	case AvroRecordOutThrottled:
		statsdKey = "topic_sorter.records_throttled"
	case AvroBytesOut:
		statsdKey = "topic_sorter.bytes"
	case AvroBytesOutThrottled:
		statsdKey = "topic_sorter.bytes_throttled"
	}
	if len(event.suffix) == 0 {
		return fmt.Sprintf("%s.%s", statsdPrefix, statsdKey)
	} else {
		return fmt.Sprintf("%s.%s.%s", statsdPrefix, statsdKey, event.suffix)
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
		var orderedKeys []string
		for k := range *gauges {
			orderedKeys = append(orderedKeys, k)
		}
		sort.Strings(orderedKeys)
		for _, k := range orderedKeys {
			log.Printf("- %s = %s\n", k, strconv.FormatInt((*gauges)[k], 10))
		}
		keysToClear = orderedKeys
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
