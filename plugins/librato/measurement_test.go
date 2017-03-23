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

package main

import (
	"net/http"
	"testing"
)

func TestMeasurementTags(t *testing.T) {
	type testCase struct {
		name        string
		value       string
		expectError bool
	}
	testCases := []testCase{
		// passing cases
		{"foo", "bar", false},
		{"label:foo", "bar", false},

		// failing cases
		{"", "bar", true},
		{"command", "()", true},
	}
	for _, tc := range testCases {
		measurement := newMeasurement()
		err := measurement.addTag(tc.name, tc.value)
		if tc.expectError && err == nil {
			t.Fatalf("Test case %+v should have failed", tc)
		}
		if !tc.expectError && err != nil {
			t.Fatalf("Test case %+v should not have failed but did: %v", tc, err)
		}
	}
}

func TestMeasurementSetValue(t *testing.T) {
	type testCase struct {
		value       interface{}
		expectError bool
	}
	testCases := []testCase{
		// passing cases
		{5, false},
		{5.0, false},
		{int(5), false},
		{int32(5), false},
		{int64(5), false},
		{float32(5), false},
		{float64(5), false},
		{"3.1415", false},

		// failing cases
		{"not a number", true},
		{new(http.Client), true},
	}
	for _, tc := range testCases {
		measurement := newMeasurement()
		err := measurement.setValue(tc.value)
		if tc.expectError && err == nil {
			t.Fatalf("Test case %+v should have failed", tc)
		}
		if !tc.expectError && err != nil {
			t.Fatalf("Test case %+v should not have failed but did: %v", tc, err)
		}
	}
}
