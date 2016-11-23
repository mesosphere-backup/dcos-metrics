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
	. "github.com/smartystreets/goconvey/convey"
)

func TestAvroRecordExtractDatapoint(t *testing.T) {
	Convey("When extracting a datapoint from an Avro record", t, func() {
		testRecord := record{
			Name: "dcos.metrics.Datapoint",
			Fields: []field{
				{
					Name:  "test-name",
					Datum: "name-field-test",
				},
				{
					Name:  "test-value",
					Datum: "value-field-test",
				},
				{
					Name:  "test-unit",
					Datum: "unit-field-test",
				},
			},
		}

		avroDatapoint := avroRecord{testRecord}
		pmmTest := producers.MetricsMessage{}

		err := avroDatapoint.extract(&pmmTest)

		Convey("Should extract the datapoint without errors", func() {
			So(err, ShouldBeNil)
			So(len(pmmTest.Datapoints), ShouldEqual, 1)
		})

		Convey("Should return the expected name and values from the datapoint", func() {
			So(pmmTest.Datapoints[0].Name, ShouldEqual, "name-field-test")
			So(pmmTest.Datapoints[0].Value, ShouldEqual, "value-field-test")
			So(pmmTest.Datapoints[0].Unit, ShouldEqual, "unit-field-test")
		})
	})
}

func TestAvroRecordExtractTags(t *testing.T) {
	Convey("When extracting tags from an Avro record", t, func() {
		testRecord := record{
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

		avroDatapoint := avroRecord{testRecord}
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
}
