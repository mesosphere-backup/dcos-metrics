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
	"flag"
	"io/ioutil"
	"os"
	"testing"

	. "github.com/smartystreets/goconvey/convey"
)

func TestMain(t *testing.T) {
	Convey("Ensure collector loads and responds", t, func() {
		So(func() { main() }, ShouldNotPanic)
	})
}

func TestDefaultConfig(t *testing.T) {
	Convey("Ensure default configuration is set properly", t, func() {
		testConfig := newConfig()

		Convey("Default polling period should be 15", func() {
			So(testConfig, ShouldEqual, 15)
		})

		Convey("HTTP profiler should be enabled by default", func() {
			So(testConfig.HTTPProfiler, ShouldBeTrue)
		})

		Convey("Kafka flag should be enabled by default", func() {
			So(testConfig.KafkaProducer, ShouldBeTrue)
		})
	})
}

func TestSetFlags(t *testing.T) {
	Convey("Ensure command line flags are applied", t, func() {
		testConfig := CollectorConfig{
			ConfigPath: "/some/default/path",
		}
		testFS := flag.NewFlagSet("", flag.PanicOnError)
		testConfig.setFlags(testFS)
		testFS.Parse([]string{"-config", "/another/config/path"})

		So(testConfig.ConfigPath, ShouldEqual, "/another/config/path")
	})
}

func TestLoadConfig(t *testing.T) {
	// Mock out and create the config file
	configContents := []byte(`
---
agent_config:
  port: 5051
  metric_topic: agent-metrics
polling_period: 5 
http_profiler: false
kafka_producer: false`)

	tmpConfig, err := ioutil.TempFile("", "testConfig")
	if err != nil {
		panic(err)
	}

	defer os.Remove(tmpConfig.Name())

	if _, err := tmpConfig.Write(configContents); err != nil {
		panic(err)
	}

	Convey("Ensure config can be loaded from a file on disk", t, func() {
		testConfig := CollectorConfig{
			ConfigPath: tmpConfig.Name(),
		}

		Convey("Loading config should not return any errors", func() {
			loadErr := testConfig.loadConfig()
			So(loadErr, ShouldBeNil)
		})

		Convey("testConfig should match mocked config file", func() {
			So(testConfig.AgentConfig.Port, ShouldEqual, 5051)
			So(testConfig.AgentConfig.Topic, ShouldEqual, "agent-metrics")
			So(testConfig.PollingPeriod, ShouldEqual, 5)
			So(testConfig.HTTPProfiler, ShouldBeFalse)
			So(testConfig.KafkaProducer, ShouldBeFalse)
		})
	})
}
