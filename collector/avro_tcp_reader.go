package collector

import (
	"github.com/linkedin/goavro"
	"log"
	"net"
)

var (
	listenEndpointFlag = StringEnvFlag("listen-endpont", "127.0.0.1:8124",
		"TCP endpoint for incoming MetricsList avro data")
	recordInputLogFlag = BoolEnvFlag("record-input-log", false,
		"Logs the parsed content of records received at -listen-endpoint")
)

// Runs a TCP socket listener which produces Avro records sent to that socket.
// Expects input which has been formatted in the Avro ODF standard.
// This function should be run as a gofunc.
func RunAvroTCPReader(recordsChan chan<- interface{}, stats chan<- StatsEvent) {
	addr, err := net.ResolveTCPAddr("tcp", *listenEndpointFlag)
	if err != nil {
		stats <- MakeEvent(TCPResolveFailed)
		log.Fatalf("Failed to parse TCP endpoint '%s': %s", *listenEndpointFlag, err)
	}
	sock, err := net.ListenTCP("tcp", addr)
	if err != nil {
		stats <- MakeEvent(TCPListenFailed)
		log.Fatalf("Failed to listen on TCP endpoint '%s': %s", *listenEndpointFlag, err)
	}

	for {
		conn, err := sock.AcceptTCP()
		if err != nil {
			stats <- MakeEvent(TCPAcceptFailed)
			log.Printf("Failed to accept connection on TCP endpoint '%s': %s\n",
				*listenEndpointFlag, err)
			continue
		}
		stats <- MakeEvent(TCPSessionOpened)
		log.Println("Launching handler for TCP connection from:", conn.RemoteAddr())
		go handleConnection(conn, recordsChan, stats)
	}
}

// ---

// Function which reads records from a TCP session.
// This function should be run as a gofunc.
func handleConnection(conn *net.TCPConn, recordsChan chan<- interface{}, stats chan<- StatsEvent) {
	conn.SetKeepAlive(true)
	defer func() {
		stats <- MakeEvent(TCPSessionClosed)
		conn.Close()
	}()

	// make an io.Reader out of conn and pass it to goavro
	avroReader, err := goavro.NewReader(goavro.FromReader(conn))
	if err != nil {
		stats <- MakeEvent(AvroReaderOpenFailed)
		log.Println("Failed to create avro reader:", err)
		return
	}
	defer func() {
		if err := avroReader.Close(); err != nil {
			stats <- MakeEvent(AvroReaderCloseFailed)
			log.Println("Failed to close avro reader:", err)
		}
	}()

	for avroReader.Scan() {
		datum, err := avroReader.Read()
		if err != nil {
			log.Printf("Cannot read avro record from %+v: %s\n", conn.RemoteAddr(), err)
			continue
		}
		stats <- MakeEvent(AvroRecordIn)
		if *recordInputLogFlag {
			log.Println("RECORD IN:", datum)
		}
		recordsChan <- datum
	}
}
