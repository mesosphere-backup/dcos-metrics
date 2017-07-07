// +build unit

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
	"math"
	"net"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/dcos/dcos-metrics/schema/metrics_schema"
	"github.com/linkedin/goavro"
	. "github.com/smartystreets/goconvey/convey"
)

var (
	mockCollectorConfig = Collector{
		ListenEndpointFlag:         "127.0.0.1:8124",
		RecordInputLogFlag:         false,
		InputLimitAmountKBytesFlag: 20480,
		InputLimitPeriodFlag:       60,
	}

	mockNodeInfo = collectors.NodeInfo{
		MesosID:   "some-mesos-id",
		ClusterID: "some-cluster-id",
		Hostname:  "some-hostname",
		IPAddress: "1.2.3.4",
	}
)

func TestNew(t *testing.T) {
	Convey("When creating a new instance of the framework collector", t, func() {
		Convey("Should return a new Collector with the default config", func() {
			f, fc := New(mockCollectorConfig, mockNodeInfo)
			So(f, ShouldHaveSameTypeAs, Collector{})
			So(fc, ShouldHaveSameTypeAs, make(chan producers.MetricsMessage))
			So(f.InputLimitAmountKBytesFlag, ShouldEqual, mockCollectorConfig.InputLimitAmountKBytesFlag)
			So(f.InputLimitPeriodFlag, ShouldEqual, mockCollectorConfig.InputLimitPeriodFlag)
			So(f.ListenEndpointFlag, ShouldEqual, mockCollectorConfig.ListenEndpointFlag)
			So(f.RecordInputLogFlag, ShouldEqual, mockCollectorConfig.RecordInputLogFlag)
			So(f.nodeInfo, ShouldResemble, mockNodeInfo)
		})
	})
}

func TestTransform(t *testing.T) {
	// Create a test record
	recDps, err := goavro.NewRecord(datapointNamespace, datapointSchema)
	if err != nil {
		panic(err)
	}
	recDps.Set("name", "some-name")
	recDps.Set("time_ms", 1000)
	recDps.Set("value", 42.0)

	recTags, err := goavro.NewRecord(tagNamespace, tagSchema)
	if err != nil {
		panic(err)
	}
	recTags.Set("key", "some-key")
	recTags.Set("value", "some-val")

	// Run the tests
	Convey("When transforming an AvroDatum to a MetricsMessage", t, func() {
		Convey("Should return a proper MetricsMessage without errors if all inputs are correct", func() {
			rec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			if err != nil {
				panic(err)
			}
			rec.Set("topic", "some-topic")
			rec.Set("tags", []interface{}{recTags})
			rec.Set("datapoints", []interface{}{recDps})

			a := AvroDatum{Record: rec, Topic: "some-topic"}
			pmm, err := a.transform(mockNodeInfo)
			So(pmm, ShouldHaveSameTypeAs, producers.MetricsMessage{})

			// If we could mock the time here, we could do a single assertion
			// using ShouldResemble (deep equals). However, it isn't easy to
			// mock the time because it's set by avroRecord.extract(). So we do
			// the next best thing: ensure we get something besides the zero
			// value for those fields.
			So(err, ShouldBeNil)
			So(pmm.Name, ShouldEqual, "dcos.metrics.app")
			So(pmm.Timestamp, ShouldBeGreaterThan, 0)
			So(len(pmm.Datapoints), ShouldEqual, 1)
			So(pmm.Datapoints[0].Name, ShouldEqual, "some-name")
			So(pmm.Datapoints[0].Value, ShouldEqual, 42.0)
			So(pmm.Datapoints[0].Timestamp, ShouldNotEqual, "")
			So(pmm.Datapoints[0].Tags, ShouldResemble, map[string]string{"some-key": "some-val"})
			So(pmm.Dimensions.MesosID, ShouldEqual, "some-mesos-id")
			So(pmm.Dimensions.ClusterID, ShouldEqual, "some-cluster-id")
			So(pmm.Dimensions.Hostname, ShouldEqual, "some-hostname")
		})

		Convey("Should return an error if AvroDatum didn't contain a goavro.Record", func() {
			a := AvroDatum{Record: make(map[string]string), Topic: "some-topic"}
			_, err = a.transform(mockNodeInfo)
			So(err, ShouldNotBeNil)
		})

		Convey("Should return an error if the record didn't contain 'tags'", func() {
			rec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			if err != nil {
				panic(err)
			}
			rec.Set("topic", "some-topic")
			rec.Set("datapoints", []interface{}{recDps})

			a := AvroDatum{Record: rec, Topic: "some-topic"}
			_, err = a.transform(mockNodeInfo)
			So(err, ShouldNotBeNil)
		})

		Convey("Should return an error if the record didn't contain 'datapoints'", func() {
			rec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			if err != nil {
				panic(err)
			}
			rec.Set("topic", "some-topic")
			rec.Set("tags", []interface{}{recTags})

			a := AvroDatum{Record: rec, Topic: "some-topic"}
			_, err = a.transform(mockNodeInfo)
			So(err, ShouldNotBeNil)
		})

		Convey("Should safely update NaN values to \"NaN\" strings", func() {
			recNan, err := goavro.NewRecord(datapointNamespace, datapointSchema)
			if err != nil {
				panic(err)
			}
			recNan.Set("name", "nan-name")
			recNan.Set("time_ms", 1000)
			recNan.Set("value", math.NaN())

			rec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			if err != nil {
				panic(err)
			}
			rec.Set("topic", "some-topic")
			rec.Set("tags", []interface{}{recTags})
			rec.Set("datapoints", []interface{}{recNan})

			a := AvroDatum{Record: rec, Topic: "some-topic"}
			pmm, err := a.transform(mockNodeInfo)
			So(err, ShouldBeNil)

			So(pmm.Datapoints[0].Name, ShouldEqual, "nan-name")
			So(pmm.Datapoints[0].Value, ShouldEqual, "NaN")
		})
	})
}

func TestRunFrameworkTCPListener(t *testing.T) {
	Convey("When running the framework TCP listener", t, func() {
		port, err := getEphemeralPort()
		if err != nil {
			panic(err)
		}

		Convey("Should run the listener without errors if valid configuration was given", func() {
			c := Collector{
				ListenEndpointFlag:         net.JoinHostPort("localhost", strconv.Itoa(port)),
				RecordInputLogFlag:         false,
				InputLimitAmountKBytesFlag: 20480,
				InputLimitPeriodFlag:       60,
			}
			go c.RunFrameworkTCPListener()
			time.Sleep(1 * time.Second) // brief delay to allow the tcp listener to start

			_, err := net.Dial("tcp", net.JoinHostPort("localhost", strconv.Itoa(port)))
			So(err, ShouldBeNil)
		})
	})
}

func TestGetTopic(t *testing.T) {
	var (
		metricListNamespace = goavro.RecordEnclosingNamespace(metricsSchema.MetricListNamespace)
		metricListSchema    = goavro.RecordSchema(metricsSchema.MetricListSchema)
	)

	Convey("When extracting the topic value from the provided Avro record", t, func() {
		Convey("Should return UNKNOWN_RECORD_TYPE if the record wasn't valid", func() {
			ts, ok := getTopic([]string{"foo"})
			So(ts, ShouldEqual, "UNKNOWN_RECORD_TYPE")
			So(ok, ShouldBeFalse)
		})

		Convey("Should return UNKNOWN_TOPIC_VAL if the field 'topic' doesn't exist", func() {
			r, err := goavro.NewRecord(
				metricListNamespace,
				goavro.RecordSchema(`{"name": "some-schema", "fields": [{"name": "foo", "type": "string"}]}`))

			if err != nil {
				panic(err)
			}
			ts, ok := getTopic(r)
			So(ts, ShouldEqual, "UNKNOWN_TOPIC_VAL")
			So(ok, ShouldBeFalse)
		})

		Convey("Should return UNKNOWN_TOPIC_TYPE if the topic string wasn't retrievable", func() {
			r, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			r.Set("topic", nil)

			if err != nil {
				panic(err)
			}
			ts, ok := getTopic(r)
			So(ts, ShouldEqual, "UNKNOWN_TOPIC_TYPE")
			So(ok, ShouldBeFalse)
		})

		Convey("Should return the topic string with no errors", func() {
			r, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			r.Set("topic", "some-topic")
			if err != nil {
				panic(err)
			}
			ts, ok := getTopic(r)
			So(ts, ShouldEqual, "some-topic")
			So(ok, ShouldBeTrue)
		})
	})
}

func TestHandleConnection(t *testing.T) {
	Convey("When handling new TCP connections", t, func() {
		port, err := getEphemeralPort()
		if err != nil {
			panic(err)
		}
		ln, err := net.Listen("tcp", net.JoinHostPort("localhost", strconv.Itoa(port)))
		if err != nil {
			panic(err)
		}
		defer ln.Close()
		time.Sleep(1 * time.Second)

		c, cc := New(mockCollectorConfig, mockNodeInfo)

		// This goroutine runs in the background waiting for a TCP connection
		// from the test below. Once the connection has been accepted,
		// handleConnection() processes the data and puts it on the recordsChan.
		// To avoid a non-deterministic sleep() here, this intentionally blocks
		// until the connection is processed, at which point we break out of the
		// loop and the goroutine finishes.
		//
		// This works mostly because we only use a single connection in this
		// test. If we test with >1 conns in the future, this will need to be
		// reworked a bit.
		go func() {
			for {
				conn, err := ln.Accept()
				if err != nil {
					panic(err)
				}
				defer conn.Close()
				c.handleConnection(conn) // blocks
				break
			}
		}()

		Convey("Should put new records of type AvroDatum on to the channel", func() {
			// Create a test record
			recDps, err := goavro.NewRecord(datapointNamespace, datapointSchema)
			if err != nil {
				panic(err)
			}
			recDps.Set("name", "some-name")
			recDps.Set("time_ms", int64(1000))
			recDps.Set("value", 42.0)

			recTags, err := goavro.NewRecord(tagNamespace, tagSchema)
			if err != nil {
				panic(err)
			}
			recTags.Set("key", "some-key")
			recTags.Set("value", "some-val")

			rec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			if err != nil {
				panic(err)
			}
			rec.Set("topic", "some-topic")
			rec.Set("tags", []interface{}{recTags})
			rec.Set("datapoints", []interface{}{recDps})

			// Encode and write the record
			conn, err := net.Dial("tcp", net.JoinHostPort("localhost", strconv.Itoa(port)))
			if err != nil {
				panic(err)
			}

			codec, err := goavro.NewCodec(metricsSchema.MetricListSchema)
			if err != nil {
				panic(err)
			}

			avroWriter, err := goavro.NewWriter(goavro.ToWriter(conn), goavro.UseCodec(codec))
			if err != nil {
				panic(err)
			}

			// Test
			avroWriter.Write(rec)
			avroWriter.Close()
			msg := <-cc // blocks

			So(msg, ShouldHaveSameTypeAs, producers.MetricsMessage{})
			So(len(msg.Datapoints), ShouldEqual, 1)
			So(msg.Dimensions.ClusterID, ShouldEqual, mockNodeInfo.ClusterID)
			So(msg.Dimensions.MesosID, ShouldEqual, mockNodeInfo.MesosID)
			So(msg.Dimensions.Hostname, ShouldEqual, mockNodeInfo.Hostname)
		})
	})

}

func TestRead(t *testing.T) {
	Convey("When reading bytes from an io.Reader", t, func() {
		Convey("Should return accurate byte counts to countingReader", func() {
			result := make([]byte, 3)
			cr := countingReader{strings.NewReader("foo"), 0}
			n, err := cr.Read(result)

			So(err, ShouldBeNil)
			So(cr.inputBytes, ShouldEqual, 3)
			So(n, ShouldEqual, cr.inputBytes)
			So(string(result), ShouldEqual, "foo")
		})
	})
}

// getEphemeralPort returns an available ephemeral port on the system.
func getEphemeralPort() (int, error) {
	addr, err := net.ResolveTCPAddr("tcp", "localhost:0")
	if err != nil {
		return 0, err
	}

	l, err := net.ListenTCP("tcp", addr)
	if err != nil {
		return 0, err
	}

	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port, nil
}
