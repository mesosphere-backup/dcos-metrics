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

	"github.com/dcos/dcos-metrics/producers"
)

func TestAvroRecordExtractDatapoint(t *testing.T) {

	var (
		testRecord = record{
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

		avroDatapoint = avroRecord{testRecord}
		pmmTest       = producers.MetricsMessage{}
	)

	err := avroDatapoint.extract(&pmmTest)
	if err != nil {
		t.Errorf("Got %s, expected no errors.", err)
	}

	if len(pmmTest.Datapoints) != 1 {
		t.Errorf("Got %s, expected 1 datapoint", len(pmmTest.Datapoints))
	}

	if pmmTest.Datapoints[0].Name != "name-field-test" {
		t.Errorf("Got %s, expected name-field-test", pmmTest.Datapoints[0].Name)
	}

	if pmmTest.Datapoints[0].Value != "value-field-test" {
		t.Errorf("Got %s, expected value-field-test", pmmTest.Datapoints[0].Value)
	}

	if pmmTest.Datapoints[0].Unit != "unit-field-test" {
		t.Errorf("Got %s, expected unit-field-test", pmmTest.Datapoints[0].Value)
	}
}

func TestAvroRecordExtractTags(t *testing.T) {
	var (
		testRecord = record{
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

		avroDatapoint = avroRecord{testRecord}
		pmmTest       = producers.MetricsMessage{
			Dimensions: producers.Dimensions{
				Labels: make(map[string]string),
			},
		}
	)

	err := avroDatapoint.extract(&pmmTest)
	if err != nil {
		t.Errorf("Got %s, expected no errors.", err)
	}

	if value, ok := pmmTest.Dimensions.Labels["tag-name-field-test"]; !ok {
		t.Errorf("Expected 'tag-name-field-test' label to exist, got %s", ok)
	} else {
		if value != "tag-value-field-test" {
			t.Errorf("Got %s, expected 'tag-value-field-test", value)
		}
	}

}
