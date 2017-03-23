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

// librato responses with code 202 will be unmarshaled into a libratoResponse
type libratoResponse struct {
	Measurements libratoMeasurements `json:"measurements"`
}

type libratoMeasurements struct {
	Summary libratoSummary `json:"summary"`
	Errors  []string       `json:"errors"`
}

type libratoSummary struct {
	Total    int64 `json:"total"`
	Accepted int64 `json:"accepted"`
	Failed   int64 `json:"failed"`
}

// for testing
func (l *libratoResponse) addError(err string) {
	l.Measurements.Summary.Failed += 1
	l.Measurements.Summary.Total += 1
	l.Measurements.Errors = append(l.Measurements.Errors, err)
}

// for testing
func (l *libratoResponse) addAccepted() {
	l.Measurements.Summary.Accepted += 1
	l.Measurements.Summary.Total += 1
}
