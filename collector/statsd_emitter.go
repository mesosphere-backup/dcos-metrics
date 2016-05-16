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

// Creates and runs a Stats Emitter which sends counts to a UDP endpoint,
// or which just emits to logs if the endpoint isn't available.
// This function should be run as a gofunc.
func RunStatsEmitter(events <-chan StatsEvent) {
	gauges := make(map[string]int64)
	statsdConn := getStatsdConn()
	ticker := time.NewTicker(time.Second * 15)
	for {
		select {
		case event := <-events:
			gauges[toStatsdLabel(event)]++
			if len(event.Suffix) != 0 {
				// also count against non-suffix bucket:
				gauges[toStatsdLabel(StatsEvent{event.Type, ""})]++
			}
		case _ = <-ticker.C:
			flushGauges(statsdConn, &gauges)
			gauges = make(map[string]int64)
		}
	}
}

// ---

func toStatsdLabel(event StatsEvent) string {
	typelabel := "UNKNOWN"
	switch event.Type {
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
		typelabel = "kafka_output.avro_record"
	case KafkaSessionClosed:
		typelabel = "kafka_output.avro_record"
	case KafkaMessageSent:
		typelabel = "kafka_output.avro_record"

	case FileOutputFailed:
		typelabel = "file_output.avro_record"
	case FileOutputWritten:
		typelabel = "file_output.avro_record"

	case AvroWriterOpenFailed:
		typelabel = "avro_output.writer_open_failures"
	case AvroRecordOut:
		typelabel = "avro_output.avro_record"
	case AvroWriterCloseFailed:
		typelabel = "avro_output.writer_close_failures"

	case AgentIPFailed:
		typelabel = "producer.agent_ip_failures"
	case AgentQueryFailed:
		typelabel = "producer.agent_query_failures"
	case AgentQueryEmpty:
		typelabel = "producer.agent_query_empty_lists"
	case AgentQueried:
		typelabel = "producer.agent_queries"
	}
	if len(event.Suffix) == 0 {
		return fmt.Sprintf("collector.%s", typelabel)
	} else {
		return fmt.Sprintf("collector.%s.%s", typelabel, event.Suffix)
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
	// insert a meta-value into the stats before sending them
	(*gauges)["collector.metrics_entries"] = int64(len(*gauges))

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
