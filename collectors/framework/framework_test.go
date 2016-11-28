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
	"testing"

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

func TestRead(t *testing.T) {

}
