package collector

import (
	"encoding/hex"
	"github.com/linkedin/goavro"
	"io"
	"log"
	"net"
	"time"
)

var (
	listenEndpointFlag = StringEnvFlag("listen-endpont", "127.0.0.1:8124",
		"TCP endpoint for incoming MetricsList avro data")
	recordInputLogFlag = BoolEnvFlag("record-input-log", false,
		"Logs the parsed content of records received at -listen-endpoint")
	recordInputHexdumpFlag = BoolEnvFlag("record-input-hexdump", false,
		"Prints a verbose hex dump of incoming records (ala 'hexdump -C')")
	inputLimitAmountKBytesFlag = IntEnvFlag("input-limit-amount-kbytes", 20480,
		"The amount of data that will be accepted from a single input in -input-limit-period. "+
			"Records from an input beyond this limit will be dropped until the period resets.")
	inputLimitPeriodFlag = IntEnvFlag("input-limit-period", 60,
		"Number of seconds over which to enforce -input-limit-amount-kbytes")
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

	reader := &countingReader{conn, 0}
	avroReader, err := goavro.NewReader(goavro.FromReader(reader))
	if err != nil {
		stats <- MakeEvent(AvroReaderOpenFailed)
		log.Println("Failed to create avro reader:", err)
		return // close connection
	}
	defer func() {
		if err := avroReader.Close(); err != nil {
			stats <- MakeEvent(AvroReaderCloseFailed)
			log.Println("Failed to close avro reader:", err)
		}
	}()

	nextInputResetTime := time.Now().Add(time.Second * time.Duration(*inputLimitPeriodFlag))
	for {
		// Waits for records to be available
		if !avroReader.Scan() {
			// Stream closed, exit
			break
		}
		now := time.Now()
		if now.After(nextInputResetTime) {
			// Limit period has transpired, reset limit count before continuing
			if reader.Count > 1024**inputLimitAmountKBytesFlag {
				log.Printf("Received %d KB from %s in the last ~%ds. Of this, %d KB was dropped due to throttling.\n",
					reader.Count/1024,
					conn.RemoteAddr(),
					*inputLimitPeriodFlag,
					reader.Count/1024-*inputLimitAmountKBytesFlag)
			} else {
				log.Printf("Received %d KB from %s in the last ~%ds\n",
					reader.Count/1024, conn.RemoteAddr(), *inputLimitPeriodFlag)
			}
			reader.Count = 0
			nextInputResetTime = now.Add(time.Second * time.Duration(*inputLimitPeriodFlag))
		}
		datum, err := avroReader.Read()
		if err != nil {
			log.Printf("Cannot read avro record from %+v: %s\n", conn.RemoteAddr(), err)
			continue
		}
		topic, _ := GetTopic(datum)
		stats <- MakeEventSuff(AvroRecordIn, topic)
		if reader.Count > 1024**inputLimitAmountKBytesFlag {
			stats <- MakeEventSuff(AvroRecordThrottled, topic)
			continue
		}
		if *recordInputLogFlag {
			log.Println("RECORD IN:", datum)
		}
		recordsChan <- datum
	}
}

// An io.Reader which provides a count of the number of bytes read, and which supports optional
// hexdumps of the data that it's reading.
type countingReader struct {
	readerImpl io.Reader
	Count      int64
}

func (cr *countingReader) Read(p []byte) (int, error) {
	n, err := cr.readerImpl.Read(p)
	//log.Printf("Read into %d => %d, %+v\n", len(p), n, err)
	if *recordInputHexdumpFlag && err == nil {
		log.Printf("Hex dump of %d input bytes:\n%sEnd dump of %d input bytes",
			len(p), hex.Dump(p), len(p))
	}
	cr.Count += int64(n)
	return n, err
}
