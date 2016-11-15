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

func AvroRecord_Extract_Datapoint_Test(t *testing.T) {

	var avroDatapoint = avroRecord{}
	var pmmTest = producers.MetricsMessage{}

	avroDatapoint[0].Name = "dcos.metrics.Datapoint"
	avroDatapoint[0].Fields[0].Datum = 123
	avroDatapoint[0].Fields[0].Name = "test-name"

	err := avroDatapoint.extract(&pmmTest)
	if err != nil {
		t.Errorf("Got %s, expected no errors.", err)
	}

	if len(pmmTest.Datapoints) != 1 {
		t.Errorf("Got %s, expected 1 datapoint", len(pmmTest.Datapoints))
	}

	if pmmTest.Datapoints[0].Name != "test-name" {
		t.Errorf("Got %s, expected test-name", pmmTest.Datapoints[0].Name)
	}

	if pmmTest.Datapoints[0].Value != "123" {
		t.Errorf("Got %s, expected 123", pmmTest.Datapoints[0].Value)
	}
}

func AvroRecord_Extract_Tags_Test(t *testing.T) {

	var avroDatapoint = avroRecord{}
	var pmmTest = producers.MetricsMessage{}

	avroDatapoint[0].Name = "dcos.metrics.Tag"
	avroDatapoint[0].Fields[0].Datum = "foo"
	avroDatapoint[0].Fields[0].Name = "test-tag"

	err := avroDatapoint.extract(&pmmTest)
	if err != nil {
		t.Errorf("Got %s, expected no errors.", err)
	}

	if len(pmmTest.Dimensions.Labels) != 1 {
		t.Errorf("Got %s, expected 1 datapoint", len(pmmTest.Dimensions.Labels))
	}

	if _, ok := pmmTest.Dimensions.Labels["test-tag"]; !ok {
		t.Errorf("Got %s, expected test-tag to exist", pmmTest.Dimensions.Labels)
	}

	if pmmTest.Dimensions.Labels["test-tag"] != "foo" {
		t.Errorf("Got %s, expected foo", pmmTest.Dimensions.Labels)
	}
}
