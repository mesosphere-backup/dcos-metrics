// Copyright 2016 Mesosphere, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package collector

import (
	"encoding/hex"
	"io"
	"log"
	"net"
	"time"

	"github.com/dcos/dcos-metrics/producers/statsd"
	"github.com/linkedin/goavro"
)

var (
	listenEndpointFlag = StringEnvFlag("listen-endpont", "127.0.0.1:8124",
		"TCP endpoint for incoming MetricsList avro data")
	recordInputLogFlag = BoolEnvFlag("record-input-log", false,
		"Logs the parsed content of records received at -listen-endpoint")
	recordInputHexdumpFlag = BoolEnvFlag("record-input-hexdump", false,
		"Prints a verbose hex dump of incoming records (ala 'hexdump -C')")

	inputLimitAmountKBytesFlag = IntEnvFlag("input-limit-amount-kbytes", 20480,
		"The amount of data that will be accepted from a given input in -input-limit-period. "+
			"Records from an input beyond this limit will be dropped until the period resets. "+
			"This value is applied on a PER-CONNECTION basis.")
	inputLimitPeriodFlag = IntEnvFlag("input-limit-period", 60,
		"Number of seconds over which to enforce -input-limit-amount-kbytes")
)

// RunAvroTCPReader runs a TCP socket listener which produces Avro records sent to that socket.
// Expects input which has been formatted in the Avro ODF standard.
// This function should be run as a gofunc.
func RunAvroTCPReader(recordsChan chan<- *AvroDatum, stats chan<- statsd.StatsEvent) {
	addr, err := net.ResolveTCPAddr("tcp", *listenEndpointFlag)
	if err != nil {
		stats <- statsd.MakeEvent(statsd.TCPResolveFailed)
		log.Fatalf("Failed to parse TCP endpoint '%s': %s", *listenEndpointFlag, err)
	}
	sock, err := net.ListenTCP("tcp", addr)
	if err != nil {
		stats <- statsd.MakeEvent(statsd.TCPListenFailed)
		log.Fatalf("Failed to listen on TCP endpoint '%s': %s", *listenEndpointFlag, err)
	}

	for {
		conn, err := sock.AcceptTCP()
		if err != nil {
			stats <- statsd.MakeEvent(statsd.TCPAcceptFailed)
			log.Printf("Failed to accept connection on TCP endpoint '%s': %s\n",
				*listenEndpointFlag, err)
			continue
		}
		stats <- statsd.MakeEvent(statsd.TCPSessionOpened)
		log.Println("Launching handler for TCP connection from:", conn.RemoteAddr())
		go handleConnection(conn, recordsChan, stats)
	}
}

// ---

// Function which reads records from a TCP session.
// This function should be run as a gofunc.
func handleConnection(conn *net.TCPConn, recordsChan chan<- *AvroDatum, stats chan<- statsd.StatsEvent) {
	conn.SetKeepAlive(true)
	defer func() {
		stats <- statsd.MakeEvent(statsd.TCPSessionClosed)
		conn.Close()
	}()

	reader := &countingReader{conn, 0}
	avroReader, err := goavro.NewReader(goavro.FromReader(reader))
	if err != nil {
		stats <- statsd.MakeEvent(statsd.AvroReaderOpenFailed)
		log.Println("Failed to create avro reader:", err)
		return // close connection
	}
	defer func() {
		if err := avroReader.Close(); err != nil {
			stats <- statsd.MakeEvent(statsd.AvroReaderCloseFailed)
			log.Println("Failed to close avro reader:", err)
		}
	}()

	nextInputResetTime := time.Now().Add(time.Second * time.Duration(*inputLimitPeriodFlag))
	var lastBytesCount int64
	var recordCount int64
	for {
		lastBytesCount = reader.inputBytes
		// Wait for records to be available:
		if !avroReader.Scan() {
			// Stream closed, exit
			break
		}
		datum, err := avroReader.Read()
		if err != nil {
			log.Printf("Cannot read avro record from %+v: %s\n", conn.RemoteAddr(), err)
			continue
		}
		topic, ok := GetTopic(datum)
		if !ok {
			stats <- statsd.MakeEvent(statsd.RecordBadTopic)
		}
		// increment counters before reader.inputBytes is modified too much
		// NOTE: inputBytes is effectively being modified by a gofunc in avroReader, so it's not a perfect measurement
		recordCount++
		approxBytesRead := reader.inputBytes - lastBytesCount
		stats <- statsd.MakeEventSuff(statsd.AvroRecordIn, topic)
		stats <- statsd.MakeEventSuffCount(statsd.AvroBytesIn, topic, int(approxBytesRead))

		// reset throttle counter if needed, before enforcing it below
		// ideally we'd use a ticker for this, but the goavro api already requires we use manual polling
		now := time.Now()
		if now.After(nextInputResetTime) {
			// Limit period has transpired, reset limit count before continuing
			if reader.inputBytes > *inputLimitAmountKBytesFlag*1024 {
				log.Printf("INPUT SUMMARY: Received %d MetricLists (%d KB) from %s in the last ~%ds. "+
					"Of this, ~%d KB was dropped due to throttling.\n",
					recordCount,
					reader.inputBytes/1024,
					conn.RemoteAddr(),
					*inputLimitPeriodFlag,
					reader.inputBytes/1024-*inputLimitAmountKBytesFlag)
			} else {
				log.Printf("INPUT SUMMARY: Received %d MetricLists (%d KB) from %s in the last ~%ds\n",
					recordCount, reader.inputBytes/1024, conn.RemoteAddr(), *inputLimitPeriodFlag)
			}
			recordCount = 0
			reader.inputBytes = 0
			nextInputResetTime = now.Add(time.Second * time.Duration(*inputLimitPeriodFlag))
		}

		if reader.inputBytes > *inputLimitAmountKBytesFlag*1024 {
			// input limit reached, skip
			stats <- statsd.MakeEventSuff(statsd.AvroRecordInThrottled, topic)
			stats <- statsd.MakeEventSuffCount(statsd.AvroBytesInThrottled, topic, int(approxBytesRead))
			continue
		}
		if *recordInputLogFlag {
			log.Println("RECORD IN:", datum)
		}
		recordsChan <- &AvroDatum{datum, topic, approxBytesRead}
	}
}

// An io.Reader which provides counts of the number of bytes read, and which supports optional
// hexdumps of the data that it's reading.
type countingReader struct {
	readerImpl io.Reader
	inputBytes int64
}

func (cr *countingReader) Read(p []byte) (int, error) {
	n, err := cr.readerImpl.Read(p)
	//log.Printf("Read into %d => %d, %+v\n", len(p), n, err)
	if *recordInputHexdumpFlag && err == nil {
		log.Printf("Hex dump of %d input bytes:\n%sEnd dump of %d input bytes",
			len(p), hex.Dump(p), len(p))
	}
	cr.inputBytes += int64(n)
	return n, err
}
