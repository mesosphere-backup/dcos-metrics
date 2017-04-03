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

package plugin

import (
	"errors"
	"fmt"
	"math"
	"strconv"
	"time"
)

// DatapointValueToFloat64 converts a Datapoint's Value from an
// interface{} to a float64.
func DatapointValueToFloat64(value interface{}) (float64, error) {
	switch value := value.(type) {
	case float64:
		return value, nil
	case float32:
		return float64(value), nil
	case int64:
		return float64(value), nil
	case int32:
		return float64(value), nil
	case int:
		return float64(value), nil
	case string:
		return strconv.ParseFloat(value, 64)
	}
	return math.NaN(), fmt.Errorf("Could not convert %+v (%T) to float64", value, value)
}

// ParseDatapointTimestamp parses an RFC3339 timestamp to a *time.Time
func ParseDatapointTimestamp(timestamp string) (*time.Time, error) {
	if timestamp == "" {
		return nil, errors.New("Received an empty timestamp")
	}
	parsed, err := time.Parse(time.RFC3339, timestamp)
	if err != nil {
		return nil, err
	}
	return &parsed, nil
}
