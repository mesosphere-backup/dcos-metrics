package main

import (
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector"
	"log"
	"net"
)

var (
	listenEndpointFlag = collector.StringEnvFlag("listen-endpont", "127.0.0.1:8124",
		"Incoming TCP endpoint for MetricsList avro data")
	logRecordInputFlag = collector.BoolEnvFlag("log-record-input", false,
		"Whether to log the parsed content of incoming records")
)

// Runs a TCP socket listener which produces Avro records sent to that socket.
// Expects input which has been formatted in the Avro ODF standard.
// This function should be run as a gofunc.
func RunAvroTCPReader(recordsChan chan<- interface{}, stats chan<- collector.StatsEvent) {
	addr, err := net.ResolveTCPAddr("tcp", *listenEndpointFlag)
	if err != nil {
		stats <- collector.MakeEvent(collector.TCPResolveFailed)
		log.Fatal("Failed to parse TCP endpoint '%s': %s", *listenEndpointFlag, err)
	}
	sock, err := net.ListenTCP("tcp", addr)
	if err != nil {
		stats <- collector.MakeEvent(collector.TCPListenFailed)
		log.Fatal("Failed to listen on TCP endpoint '%s': %s", *listenEndpointFlag, err)
	}

	for {
		conn, err := sock.AcceptTCP()
		if err != nil {
			stats <- collector.MakeEvent(collector.TCPAcceptFailed)
			log.Printf("Failed to accept connection on TCP endpoint '%s': %s\n",
				*listenEndpointFlag, err)
			continue
		}
		stats <- collector.MakeEvent(collector.TCPSessionOpened)
		log.Println("Opening handler for TCP connection from:", conn.RemoteAddr())
		go handleConnection(conn, recordsChan, stats)
	}
}

// ---

// Function which reads records from a TCP session.
// This function should be run as a gofunc.
func handleConnection(conn *net.TCPConn, recordsChan chan<- interface{},
	stats chan<- collector.StatsEvent) {
	conn.SetKeepAlive(true)
	defer func() {
		stats <- collector.MakeEvent(collector.TCPSessionClosed)
		conn.Close()
	}()

	// make an io.Reader out of conn and pass it to goavro
	avroReader, err := goavro.NewReader(goavro.FromReader(conn))
	if err != nil {
		stats <- collector.MakeEvent(collector.AvroReaderOpenFailed)
		log.Println("Failed to create avro reader:", err)
		return
	}
	defer func() {
		if err := avroReader.Close(); err != nil {
			stats <- collector.MakeEvent(collector.AvroReaderCloseFailed)
			log.Println("Failed to close avro reader:", err)
		}
	}()

	for avroReader.Scan() {
		datum, err := avroReader.Read()
		if err != nil {
			log.Printf("Cannot read avro record from %+v: %s\n", conn.RemoteAddr(), err)
			continue
		}
		stats <- collector.MakeEvent(collector.AvroRecordIn)
		if *logRecordInputFlag {
			log.Println("RECORD IN:", datum)
		}
		recordsChan <- datum
	}
}
