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

package collector

import (
	"testing"

	"github.com/linkedin/goavro"
	. "github.com/smartystreets/goconvey/convey"
)

func TestNewAvroData(t *testing.T) {
	Convey("Should return a new avroData type", t, func() {
		ad := newAvroData()
		So(ad, ShouldResemble, avroData{recs: []interface{}{}, approxBytes: 0})
	})
}

func TestCollector_GetTopic(t *testing.T) {
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
