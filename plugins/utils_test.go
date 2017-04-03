// Copyright 2017 Mesosphere, Inc.
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

package plugin

import (
	"net/http"
	"testing"
	"time"
)

func TestDatapointValueToFloat64(t *testing.T) {
	type testCase struct {
		value       interface{}
		expected    float64
		expectError bool
	}
	testCases := []testCase{
		{value: "5", expected: 5.0},
		{value: "3.14", expected: 3.14},
		{value: 5, expected: 5.0},
		{value: int32(5), expected: 5.0},
		{value: int64(5), expected: 5.0},
		{value: float32(5), expected: 5.0},
		{value: float64(5), expected: 5.0},
		// expected to fail
		{value: "not a number", expectError: true},
		{value: new(http.Client), expectError: true},
	}
	for _, tc := range testCases {
		res, err := DatapointValueToFloat64(tc.value)
		if tc.expectError && err == nil {
			t.Fatalf("Test case %+v should have failed", tc)
		}
		if !tc.expectError && err != nil {
			t.Fatalf("Test case %+v should not have failed but did: %v", tc, err)
		}
		if tc.expectError && err != nil {
			// this is good. don't test the result
			continue
		}
		if res != tc.expected {
			t.Fatalf("Test case %+v parsed unexpected value: %v", tc, res)
		}
	}
}

func TestParseDatapointTimestamp(t *testing.T) {
	type testCase struct {
		value       string
		expected    time.Time
		expectError bool
	}
	expected, err := time.Parse(time.RFC3339, "2017-04-03T15:05:07-04:00")
	if err != nil {
		t.Fatal(err)
	}
	testCases := []testCase{
		{value: "2017-04-03T15:05:07-04:00", expected: expected},
		// expected to fail
		{value: "", expectError: true},
		{value: "this is not a time", expectError: true},
	}
	for _, tc := range testCases {
		res, err := ParseDatapointTimestamp(tc.value)
		if tc.expectError && err == nil {
			t.Fatalf("Test case %+v should have failed", tc)
		}
		if !tc.expectError && err != nil {
			t.Fatalf("Test case %+v should not have failed but did: %v", tc, err)
		}
		if tc.expectError && err != nil {
			// this is good. don't test the result
			continue
		}
		resUnix := (*res).Unix()
		expectedUnix := tc.expected.Unix()
		if resUnix != expectedUnix {
			t.Fatalf("Test case %+v parsed unexpected value: %v", tc, res)
		}
	}
}
