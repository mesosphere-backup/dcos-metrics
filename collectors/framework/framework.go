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

package framework

import (
	"encoding/hex"
	"errors"
	"io"
	"net"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/linkedin/goavro"
)

var (
	listenEndpointFlag         = "127.0.0.1:8124"
	recordInputLogFlag         = false
	recordInputHexdumpFlag     = false
	inputLimitAmountKBytesFlag = 20480
	inputLimitPeriodFlag       = 60
	fwColLog                   = log.WithFields(log.Fields{
		"collector": "framework",
	})
)

// AvroDatum conveys the avro data coming in on our TCP listening channel.
type AvroDatum struct {
	// *goavro.Record
	Record interface{}
	// the topic that the Record requested
	Topic string
	// the approximate byte size of the original Record, if known
	ApproxBytes int64
}

// Transform creates a MetricsMessage from the Avro data coming in on our TCP
// channel.
func (a *AvroDatum) Transform(mesosID string, clusterID string, ipaddress string) (producers.MetricsMessage, error) {
	var (
		tagData       = avroRecord{}
		datapointData = avroRecord{}
		// Initialize the metrics message
		pmm = producers.MetricsMessage{
			Name: "dcos.metrics.app",
			Dimensions: producers.Dimensions{
				ClusterID: clusterID,
				MesosID:   mesosID,
				Hostname:  ipaddress,
				Labels:    make(map[string]string),
			},
		}
	)

	// Get the raw avro record
	record, ok := a.Record.(*goavro.Record)
	if !ok {
		return pmm, errors.New("Avro record is not of type *goavro.Record")
	}

	// Get tags from record
	tags, err := record.Get("tags")
	if err != nil {
		return pmm, err
	}

	if err := tagData.createObjectFromRecord(tags); err != nil {
		return pmm, err
	}

	if err := tagData.extract(&pmm); err != nil {
		return pmm, err
	}

	// Get datapoints from record, create strictly typed object, and
	// extract datapoints to metricsMessage.Datapoints
	dps, err := record.Get("datapoints")
	if err != nil {
		return pmm, err
	}

	if err := datapointData.createObjectFromRecord(dps); err != nil {
		return pmm, err
	}

	if err := datapointData.extract(&pmm); err != nil {
		return pmm, err
	}

	return pmm, nil
}

// RunFrameworkTCPListener runs a TCP socket listener which produces Avro
// records sent to that socket. Expects input which has been formatted in the
// Avro ODF standard. This function should be run as a gofunc.
func RunFrameworkTCPListener(recordsChan chan *AvroDatum) {
	fwColLog.Info("Starting TCP listener for framework metric collection")
	addr, err := net.ResolveTCPAddr("tcp", listenEndpointFlag)
	if err != nil {
		fwColLog.Errorf("Failed to parse TCP endpoint '%s': %s", listenEndpointFlag, err)
	}
	fwColLog.Debugf("Attempting to bind on %+v", addr)
	sock, err := net.ListenTCP("tcp", addr)
	if err != nil {
		fwColLog.Errorf("Failed to listen on TCP endpoint '%s': %s", listenEndpointFlag, err)
	}
	for {
		fwColLog.Debug("Waiting for connections from Mesos Metrics Module...")
		conn, err := sock.Accept()
		if err != nil {
			fwColLog.Errorf("Failed to accept connection on TCP endpoint '%s': %s\n", listenEndpointFlag, err)
			continue
		}
		fwColLog.Infof("Launching handler for TCP connection from: %+v", conn.RemoteAddr())
		go handleConnection(conn, recordsChan)
	}
}

// Function which reads records from a TCP session.
// This function should be run as a gofunc.
func handleConnection(conn net.Conn, recordsChan chan<- *AvroDatum) {
	defer conn.Close()
	reader := &countingReader{conn, 0}
	avroReader, err := goavro.NewReader(goavro.FromReader(reader))
	if err != nil {
		fwColLog.Errorf("Failed to create avro reader: %s", err)
		return // close connection
	}
	defer func() {
		if err := avroReader.Close(); err != nil {
			fwColLog.Errorf("Failed to close avro reader: %s", err)
		}
	}()

	nextInputResetTime := time.Now().Add(time.Second * time.Duration(inputLimitPeriodFlag))
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
			fwColLog.Errorf("Cannot read avro record from %+v: %s\n", conn.RemoteAddr(), err)
			continue
		}

		// TODO(roger): !ok
		topic, _ := GetTopic(datum)

		// increment counters before reader.inputBytes is modified too much
		// NOTE: inputBytes is effectively being modified by a gofunc in avroReader, so it's not a perfect measurement
		recordCount++
		approxBytesRead := reader.inputBytes - lastBytesCount

		// reset throttle counter if needed, before enforcing it below
		// ideally we'd use a ticker for this, but the goavro api already requires we use manual polling
		now := time.Now()
		if now.After(nextInputResetTime) {
			// Limit period has transpired, reset limit count before continuing
			// TODO(MALNICK) Don't case like this...
			if reader.inputBytes > int64(inputLimitAmountKBytesFlag)*1024 {
				fwColLog.Debugf("Received %d MetricLists (%d KB) from %s in the last ~%ds. "+
					"Of this, ~%d KB was dropped due to throttling.\n",
					recordCount,
					reader.inputBytes/1024,
					conn.RemoteAddr(),
					inputLimitPeriodFlag,
					// TODO (MALNICK) don't cast like this..
					reader.inputBytes/1024-int64(inputLimitAmountKBytesFlag))
			} else {
				fwColLog.Debugf("Received %d MetricLists (%d KB) from %s in the last ~%ds\n",
					recordCount, reader.inputBytes/1024, conn.RemoteAddr(), inputLimitPeriodFlag)
			}
			recordCount = 0
			reader.inputBytes = 0
			nextInputResetTime = now.Add(time.Second * time.Duration(inputLimitPeriodFlag))
		}

		// TODO (MALNICK) Don't cast like this
		if reader.inputBytes > int64(inputLimitAmountKBytesFlag)*1024 {
			// input limit reached, skip
			continue
		}
		recordsChan <- &AvroDatum{datum, topic, approxBytesRead}
	}
}

// GetTopic returns the requested topic from an Avro record.
func GetTopic(obj interface{}) (string, bool) {
	record, ok := obj.(*goavro.Record)
	if !ok {
		return "UNKNOWN_RECORD_TYPE", false
	}
	topicObj, err := record.Get("topic")
	if err != nil {
		return "UNKNOWN_TOPIC_VAL", false
	}
	topicStr, ok := topicObj.(string)
	if !ok {
		return "UNKNOWN_TOPIC_TYPE", false
	}
	return topicStr, true
}

// An io.Reader which provides counts of the number of bytes read, and which supports optional
// hexdumps of the data that it's reading.
type countingReader struct {
	readerImpl io.Reader
	inputBytes int64
}

func (cr *countingReader) Read(p []byte) (int, error) {
	n, err := cr.readerImpl.Read(p)
	if recordInputHexdumpFlag && err == nil {
		fwColLog.Debugf("Hex dump of %d input bytes:\n%sEnd dump of %d input bytes",
			len(p), hex.Dump(p), len(p))
	}
	cr.inputBytes += int64(n)
	return n, err
}
