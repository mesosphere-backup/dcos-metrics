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
	"fmt"
	"net"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/producers"
	"github.com/dcos/dcos-metrics/schema/metrics_schema"
	"github.com/linkedin/goavro"
	. "github.com/smartystreets/goconvey/convey"
)

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
			pmm, err := a.Transform("some-mesos-id", "some-cluster-id", "1.2.3.4")
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
			So(pmm.Dimensions.MesosID, ShouldEqual, "some-mesos-id")
			So(pmm.Dimensions.ClusterID, ShouldEqual, "some-cluster-id")
			So(pmm.Dimensions.Hostname, ShouldEqual, "1.2.3.4")
			So(pmm.Dimensions.Labels, ShouldResemble, map[string]string{"some-key": "some-val"})
		})

		Convey("Should return an error if AvroDatum didn't contain a goavro.Record", func() {
			a := AvroDatum{Record: make(map[string]string), Topic: "some-topic"}
			_, err = a.Transform("some-mesos-id", "some-cluster-id", "1.2.3.4")
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
			_, err = a.Transform("some-mesos-id", "some-cluster-id", "1.2.3.4")
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
			_, err = a.Transform("some-mesos-id", "some-cluster-id", "1.2.3.4")
			So(err, ShouldNotBeNil)
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
				listenEndpointFlag:         net.JoinHostPort("localhost", strconv.Itoa(port)),
				recordInputLogFlag:         false,
				inputLimitAmountKBytesFlag: 20480,
				inputLimitPeriodFlag:       60,
			}
			go c.RunFrameworkTCPListener(make(chan *AvroDatum))
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
			ts, ok := GetTopic([]string{"foo"})
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
			ts, ok := GetTopic(r)
			So(ts, ShouldEqual, "UNKNOWN_TOPIC_VAL")
			So(ok, ShouldBeFalse)
		})

		Convey("Should return UNKNOWN_TOPIC_TYPE if the topic string wasn't retrievable", func() {
			r, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			r.Set("topic", nil)

			if err != nil {
				panic(err)
			}
			ts, ok := GetTopic(r)
			So(ts, ShouldEqual, "UNKNOWN_TOPIC_TYPE")
			So(ok, ShouldBeFalse)
		})

		Convey("Should return the topic string with no errors", func() {
			r, err := goavro.NewRecord(metricListNamespace, metricListSchema)
			r.Set("topic", "some-topic")
			if err != nil {
				panic(err)
			}
			ts, ok := GetTopic(r)
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

		c := Collector{}
		recordsChan := make(chan *AvroDatum)

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
				c.handleConnection(conn, recordsChan) // blocks
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
			records := <-recordsChan // blocks

			tags, err := records.Record.(*goavro.Record).Get("tags")
			if err != nil {
				panic(err)
			}
			dps, err := records.Record.(*goavro.Record).Get("datapoints")
			if err != nil {
				panic(err)
			}

			So(records.ApproxBytes, ShouldBeGreaterThan, 0)
			So(records.Topic, ShouldEqual, "some-topic")
			So(fmt.Sprintf("%+v", tags), ShouldEqual, "[{dcos.metrics.Tag: [dcos.metrics.key: some-key, dcos.metrics.value: some-val]}]")
			So(fmt.Sprintf("%+v", dps), ShouldEqual, "[{dcos.metrics.Datapoint: [dcos.metrics.name: some-name, dcos.metrics.time_ms: 1000, dcos.metrics.value: 42]}]")
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
