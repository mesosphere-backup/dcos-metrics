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
	"testing"

	"github.com/dcos/dcos-metrics/producers"
	"github.com/dcos/dcos-metrics/schema/metrics_schema"
	"github.com/linkedin/goavro"
	. "github.com/smartystreets/goconvey/convey"
)

var (
	metricListNamespace = goavro.RecordEnclosingNamespace(metricsSchema.MetricListNamespace)
	metricListSchema    = goavro.RecordSchema(metricsSchema.MetricListSchema)
	datapointNamespace  = goavro.RecordEnclosingNamespace(metricsSchema.DatapointNamespace)
	datapointSchema     = goavro.RecordSchema(metricsSchema.DatapointSchema)
	tagNamespace        = goavro.RecordEnclosingNamespace(metricsSchema.TagNamespace)
	tagSchema           = goavro.RecordSchema(metricsSchema.TagSchema)
)

var (
	testDatapoint = record{
		Name: "dcos.metrics.Datapoint",
		Fields: []field{
			{
				Name:  "test-name",
				Datum: "name-field-test",
			},
			{
				Name:  "test-unit",
				Datum: "unit-field-test",
			},
			{
				Name:  "test-value",
				Datum: "value-field-test",
			},
		},
	}
	testTag = record{
		Name: "dcos.metrics.Tag",
		Fields: []field{
			{
				Name:  "test-tag-name",
				Datum: "tag-name-field-test",
			},
			{
				Name:  "test-tag-value",
				Datum: "tag-value-field-test",
			},
		},
	}
)

func TestExtract(t *testing.T) {
	Convey("When calling extract() on an avroRecord", t, func() {
		Convey("Should return an error if length of ar is 0", func() {
			ar := avroRecord{}
			err := ar.extract(&producers.MetricsMessage{})
			So(err, ShouldNotBeNil)
		})
	})

	Convey("When extracting a datapoint from an Avro record", t, func() {
		avroDatapoint := avroRecord{testDatapoint}
		pmmTest := producers.MetricsMessage{}
		err := avroDatapoint.extract(&pmmTest)

		Convey("Should extract the datapoint without errors", func() {
			So(err, ShouldBeNil)
			So(len(pmmTest.Datapoints), ShouldEqual, 1)
		})

		// TODO(roger): the datapoint schema does not contain any fields
		// allowing for the sender to specify units. Therefore we default
		// to the zero value, an empty string.
		Convey("Should return the expected name and values from the datapoint", func() {
			So(pmmTest.Datapoints[0].Name, ShouldEqual, "name-field-test")
			So(pmmTest.Datapoints[0].Value, ShouldEqual, "value-field-test")
			So(pmmTest.Datapoints[0].Unit, ShouldEqual, "")
		})
	})

	Convey("When extracting tags from an Avro record", t, func() {
		avroDatapoint := avroRecord{testTag}
		pmmTest := producers.MetricsMessage{
			Dimensions: producers.Dimensions{
				Labels: make(map[string]string),
			},
		}

		Convey("Should extract the tag without errors", func() {
			err := avroDatapoint.extract(&pmmTest)
			value, ok := pmmTest.Dimensions.Labels["tag-name-field-test"]

			So(err, ShouldBeNil)
			So(ok, ShouldBeTrue)
			So(value, ShouldEqual, "tag-value-field-test")
		})
	})

	Convey("When analyzing the field types in a record", t, func() {
		Convey("Should return an error if the field type was empty", func() {
			ar := avroRecord{record{Name: ""}}
			err := ar.extract(&producers.MetricsMessage{})
			So(err, ShouldNotBeNil)
		})

		Convey("Should return an error for an unknown field type", func() {
			ar := avroRecord{record{Name: "not-dcos.not-metrics.not-Type"}}
			err := ar.extract(&producers.MetricsMessage{})
			So(err, ShouldNotBeNil)
		})
	})
}

func TestCreateObjectFromRecord(t *testing.T) {
	// Create a test record
	recDps, err := goavro.NewRecord(datapointNamespace, datapointSchema)
	if err != nil {
		panic(err)
	}
	recDps.Set("name", "some-name")
	recDps.Set("time_ms", 1000)
	recDps.Set("value", 42.0)

	recNan, err := goavro.NewRecord(datapointNamespace, datapointSchema)
	if err != nil {
		panic(err)
	}
	recDps.Set("name", "nan-name")
	recDps.Set("time_ms", 1000)
	recDps.Set("value", math.NaN())

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

	// Run the test
	Convey("When creating an avroRecord object from an actual Avro record", t, func() {
		Convey("Should return the provided data in the expected structure without errors", func() {
			ar := avroRecord{}
			tags, err := rec.Get("tags")
			if err != nil {
				panic(err)
			}

			err = ar.createObjectFromRecord(tags)
			So(err, ShouldBeNil)
			So(ar, ShouldResemble, avroRecord{
				record{
					Name: "dcos.metrics.Tag",
					Fields: []field{
						field{
							Name:  "dcos.metrics.key",
							Datum: "some-key",
						},
						field{
							Name:  "dcos.metrics.value",
							Datum: "some-val",
						},
					},
				},
			})
		})

		Convey("Should return an error if the transformation failed", func() {
			type badData struct {
				SomeKey string
				SomeVal string
			}
			bd := badData{
				SomeKey: "foo-key",
				SomeVal: "foo-val",
			}
			ar := avroRecord{}
			err := ar.createObjectFromRecord(bd)
			So(err, ShouldNotBeNil)
		})

		Convey("Should drop well-formed datapoints with NaN values", func() {
			ar := avroRecord{}
			err = ar.createObjectFromRecord([]interface{}{recDps, recNan})

			So(err, ShouldBeNil)
			So(ar, ShouldResemble, avroRecord{
				record{
					Name: "dcos.metrics.Datapoint",
					Fields: []field{
						field{
							Name:  "dcos.metrics.name",
							Datum: "some-key",
						},
						field{
							Name:  "dcos.metrics.value",
							Datum: 42.0,
						},
					},
				},
			})
		})
	})
}
