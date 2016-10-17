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

	. "github.com/smartystreets/goconvey/convey"
)

func TestNewAgent(t *testing.T) {
	Convey("When establishing a new agentPoller", t, func() {
		Convey("When given an improper IP address", func() {
			_, err := NewAgent("", 10000, 60, "some-metrics")

			Convey("Should return an error", func() {
				So(err, ShouldNotBeNil)
			})
		})

		Convey("When given an improper port", func() {
			_, err := NewAgent("1.2.3.4", 1023, 60, "some-metrics")

			Convey("Should return an error", func() {
				So(err, ShouldNotBeNil)
			})
		})

		Convey("When given an improper pollPeriod", func() {
			_, err := NewAgent("1.2.3.4", 1024, 0, "some-metrics")

			Convey("Should return an error", func() {
				So(err, ShouldNotBeNil)
			})
		})

		Convey("When given an improper topic", func() {
			_, err := NewAgent("1.2.3.4", 1024, 60, "")

			Convey("Should return an error", func() {
				So(err, ShouldNotBeNil)
			})
		})

		Convey("When given proper inputs, should return an agent", func() {
			a, err := NewAgent("1.2.3.4", 10000, 60, "some-metrics")
			So(a, ShouldHaveSameTypeAs, Agent{})
			So(err, ShouldBeNil)
		})
	})
}

func TestGetIP(t *testing.T) {

}

func TestGetContainerMetrics(t *testing.T) {

}

func TestGetSystemMetrics(t *testing.T) {

}

func TestGetAgentState(t *testing.T) {

}

func TestGetJSONFromAgent(t *testing.T) {

}
