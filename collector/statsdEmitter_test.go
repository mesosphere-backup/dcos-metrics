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
	"fmt"
	"testing"

	. "github.com/smartystreets/goconvey/convey"
)

func TestToStatsdLabel(t *testing.T) {
	Convey("When converting a StatsEvent to a StatsD label", t, func() {

		// Spot check a few event types
		Convey("Known events should produce valid labels", func() {
			So(toStatsdLabel(
				StatsEvent{evttype: 2}), // 2 => TCPResolveFailed
				ShouldEqual, fmt.Sprintf("%s.%s", statsdPrefix, "tcp_input.tcp_resolve_failures"))
			So(toStatsdLabel(
				StatsEvent{evttype: 13}), // 13 => KafkaLookupFailed
				ShouldEqual, fmt.Sprintf("%s.%s", statsdPrefix, "kafka_output.framework_lookup_failures"))
			So(toStatsdLabel(
				StatsEvent{evttype: 19}), // 19 => AgentIPLookup
				ShouldEqual, fmt.Sprintf("%s.%s", statsdPrefix, "agent_poll.ip_lookups"))
			So(toStatsdLabel(
				StatsEvent{evttype: 27}), // 27 => RecordNoAgentStateAvailable
				ShouldEqual, fmt.Sprintf("%s.%s", statsdPrefix, "topic_sorter.no_agent_state_available"))
		})

		Convey("Unknown events should produce UNKNOWN label", func() {
			So(toStatsdLabel(StatsEvent{evttype: 9000}),
				ShouldEqual, fmt.Sprintf("%s.%s", statsdPrefix, "UNKNOWN"))
		})

		Convey("Events with a suffix should contain the suffix", func() {
			So(toStatsdLabel(StatsEvent{evttype: 9000, suffix: "bar"}),
				ShouldEqual, fmt.Sprintf("%s.%s.%s", statsdPrefix, "UNKNOWN", "bar"))
		})

		Convey("Events without a suffix should output the bare key", func() {
			So(toStatsdLabel(StatsEvent{evttype: 9000}),
				ShouldEqual, fmt.Sprintf("%s.%s", statsdPrefix, "UNKNOWN"))
		})
	})
}
