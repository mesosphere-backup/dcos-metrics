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
	"errors"
	"fmt"
	"math"
	"regexp"
	"strconv"
)

// measurement represents each point in a time series
type measurement struct {
	Name  string            `json:"name"`
	Value float64           `json:"value"`
	Tags  map[string]string `json:"tags"`
	Time  int64             `json:"time"`
}

func newMeasurement() *measurement {
	return &measurement{
		Tags: make(map[string]string),
	}
}

func (m *measurement) floorTime(interval int64) {
	m.Time = m.Time / interval * interval
}

func (m *measurement) String() string {
	return m.Name
}

func (m *measurement) setValue(value interface{}) error {
	val, err := m.toFloat64(value)
	if err != nil {
		return fmt.Errorf("Could not set value: %v", err)
	}
	m.Value = val
	return nil
}

func (m *measurement) addTag(name string, value string) error {
	matched, err := regexp.MatchString(`^[-.:_\w]+\z{1,64}$`, name)
	if err != nil {
		return fmt.Errorf("Error occurred matching tag name: %v", err)
	}
	if !matched {
		return fmt.Errorf("Tag name '%s' is not valid", name)
	}
	matched, err = regexp.MatchString(`^[-.:_\\/\w ]{1,255}$`, value)
	if err != nil {
		return fmt.Errorf("Error occurred matching tag value: %v", err)
	}
	if !matched {
		return fmt.Errorf("Tag value '%s' is not valid", value)
	}
	m.Tags[name] = value
	return nil
}

func (m *measurement) toFloat64(value interface{}) (float64, error) {
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

func (m *measurement) validate() error {
	if len(m.Tags) == 0 {
		return errors.New("Must have at least one tag")
	}
	if len(m.Name) == 0 {
		return errors.New("Must have a non-empty name")
	}
	if m.Time <= 0 {
		return errors.New("Time must be >= 0")
	}
	return nil
}