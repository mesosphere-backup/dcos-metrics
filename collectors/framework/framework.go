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
	"fmt"
	"io"
	"math"
	"net"
	"time"

	log "github.com/Sirupsen/logrus"

	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/linkedin/goavro"
)

// TODO(roger): avoid global variables here
var (
	fwColLog               = log.WithFields(log.Fields{"collector": "framework"})
	recordInputHexdumpFlag = false
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

// Collector defines the structure of the framework collector and facilitates
// mocking the various configuration options during testing.
type Collector struct {
	ListenEndpointFlag         string
	RecordInputLogFlag         bool
	InputLimitAmountKBytesFlag int
	InputLimitPeriodFlag       int

	metricsChan chan producers.MetricsMessage
	nodeInfo    collectors.NodeInfo
}

// countingReader is an io.Reader that provides counts of the number of bytes
// read, and which supports optional hexdumps of the data that it's reading.
type countingReader struct {
	readerImpl io.Reader
	inputBytes int64
}

// New returns a new instance of the framework collector.
func New(cfg Collector, nodeInfo collectors.NodeInfo) (Collector, chan producers.MetricsMessage) {
	c := cfg
	c.nodeInfo = nodeInfo
	c.metricsChan = make(chan producers.MetricsMessage)
	return c, c.metricsChan
}

// RunFrameworkTCPListener runs a TCP socket listener which produces Avro
// records sent to that socket. Expects input which has been formatted in the
// Avro ODF standard. This function should be run as a gofunc.
func (c *Collector) RunFrameworkTCPListener() {
	fwColLog.Info("Starting TCP listener for framework metric collection")
	addr, err := net.ResolveTCPAddr("tcp", c.ListenEndpointFlag)
	if err != nil {
		fwColLog.Errorf("Failed to parse TCP endpoint '%s': %s", c.ListenEndpointFlag, err)
	}
	fwColLog.Debugf("Attempting to bind on %+v", addr)
	sock, err := net.ListenTCP("tcp", addr)
	if err != nil {
		fwColLog.Errorf("Failed to listen on TCP endpoint '%s': %s", c.ListenEndpointFlag, err)
	}
	for {
		fwColLog.Debug("Waiting for connections from Mesos Metrics Module...")
		conn, err := sock.Accept()
		if err != nil {
			fwColLog.Errorf("Failed to accept connection on TCP endpoint '%s': %s\n", c.ListenEndpointFlag, err)
			continue
		}
		fwColLog.Infof("Launching handler for TCP connection from: %+v", conn.RemoteAddr())
		go c.handleConnection(conn)
	}
}

// handleConnection reads records from a TCP session.
// This function should be run as a gofunc.
func (c *Collector) handleConnection(conn net.Conn) {
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

	nextInputResetTime := time.Now().Add(time.Second * time.Duration(c.InputLimitPeriodFlag))
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
		topic, _ := getTopic(datum)

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
			if reader.inputBytes > int64(c.InputLimitAmountKBytesFlag)*1024 {
				fwColLog.Debugf("Received %d MetricLists (%d KB) from %s in the last ~%ds. "+
					"Of this, ~%d KB was dropped due to throttling.\n",
					recordCount,
					reader.inputBytes/1024,
					conn.RemoteAddr(),
					c.InputLimitPeriodFlag,
					// TODO (MALNICK) don't cast like this..
					reader.inputBytes/1024-int64(c.InputLimitAmountKBytesFlag))
			} else {
				fwColLog.Debugf("Received %d MetricLists (%d KB) from %s in the last ~%ds\n",
					recordCount, reader.inputBytes/1024, conn.RemoteAddr(), c.InputLimitPeriodFlag)
			}
			recordCount = 0
			reader.inputBytes = 0
			nextInputResetTime = now.Add(time.Second * time.Duration(c.InputLimitPeriodFlag))
		}

		// TODO (MALNICK) Don't cast like this
		if reader.inputBytes > int64(c.InputLimitAmountKBytesFlag)*1024 {
			// input limit reached, skip
			continue
		}

		ad := &AvroDatum{datum, topic, approxBytesRead}
		pmm, err := ad.transform(c.nodeInfo)
		if err != nil {
			fwColLog.Error(err)
		}
		c.metricsChan <- pmm
	}
}

// transform creates a MetricsMessage from the Avro data coming in on our TCP channel.
func (a *AvroDatum) transform(nodeInfo collectors.NodeInfo) (producers.MetricsMessage, error) {
	var (
		tagData       = avroRecord{}
		datapointData = avroRecord{}
		// Initialize the metrics message
		pmm = producers.MetricsMessage{
			Name: producers.AppMetricPrefix,
			Dimensions: producers.Dimensions{
				ClusterID: nodeInfo.ClusterID,
				MesosID:   nodeInfo.MesosID,
				Hostname:  nodeInfo.Hostname,
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

	decodedDps, ok := dps.([]interface{})
	if !ok {
		return pmm, fmt.Errorf("Could not decode datapoints %v", dps)
	}

	const metricsValueKey = "dcos.metrics.value"
	for _, dp := range decodedDps {
		dpr, ok := dp.(*goavro.Record)
		if !ok {
			fwColLog.Debugf("Bad datapoint record encountered: %v", dp)
			continue
		}
		value, err := dpr.Get(metricsValueKey)
		if err != nil {
			fwColLog.Debugf("Datum without value encountered: %v", dpr)
			continue
		}
		// Avoid marshalling to JSON with NaN values, which
		// chokes the go json library.
		if v, ok := value.(float64); ok {
			if math.IsNaN(v) {
				dpr.Set(metricsValueKey, "NaN")
			}
		}
	}

	if err := datapointData.createObjectFromRecord(dps); err != nil {
		return pmm, err
	}

	if err := datapointData.extract(&pmm); err != nil {
		return pmm, err
	}

	return pmm, nil
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

// getTopic returns the requested topic from an Avro record.
func getTopic(obj interface{}) (string, bool) {
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
