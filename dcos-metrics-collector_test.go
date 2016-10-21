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

package main

import (
	"flag"
	"io/ioutil"
	"os"
	"testing"

	yaml "gopkg.in/yaml.v2"

	. "github.com/smartystreets/goconvey/convey"
)

func TestMain(t *testing.T) {
	Convey("Ensure collector loads and responds", t, nil)
}

func TestDefaultConfig(t *testing.T) {
	Convey("Ensure default configuration is set properly", t, func() {
		testConfig := newConfig()

		Convey("Default polling period should be 15", func() {
			So(testConfig.Collector.PollingPeriod, ShouldEqual, 15)
		})

		Convey("HTTP profiler should be enabled by default", func() {
			So(testConfig.Collector.HTTPProfiler, ShouldBeTrue)
		})
	})
}

func TestSetFlags(t *testing.T) {
	Convey("Ensure command line flags are applied", t, func() {
		testConfig := Config{
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
collector:
  agent_config:
    port: 5051
    kafka_topic: agent-metrics
  polling_period: 5
  http_profiler: false
producers:
  kafka:
    brokers: 'foo'
`)

	tmpConfig, err := ioutil.TempFile("", "testConfig")
	if err != nil {
		panic(err)
	}

	defer os.Remove(tmpConfig.Name())

	if _, err := tmpConfig.Write(configContents); err != nil {
		panic(err)
	}

	Convey("Ensure config can be loaded from a file on disk", t, func() {
		testConfig := Config{
			ConfigPath: tmpConfig.Name(),
		}

		Convey("testConfig should match mocked config file", func() {
			loadErr := testConfig.loadConfig()
			So(loadErr, ShouldBeNil)

			So(testConfig.Collector.AgentConfig.Port, ShouldEqual, 5051)
			So(testConfig.Collector.AgentConfig.KafkaTopic, ShouldEqual, "agent-metrics")
			So(testConfig.Collector.PollingPeriod, ShouldEqual, 5)
			So(testConfig.Collector.HTTPProfiler, ShouldBeFalse)
			So(testConfig.Producers.KafkaProducerConfig.Brokers, ShouldEqual, "foo")
		})
	})
}

func TestProducerIsConfigured(t *testing.T) {
	Convey("When analyzing a ProducersConfig struct to determine if a producer is configured", t, func() {
		Convey("Should return true if a producer configuration was provided", func() {
			var c Config
			mockConfig := []byte(`
---
producers:
    http:
        someConfig: 'someVal'
    kafka:
        someConfig: 'someVal'
    statsd:
        someConfig: 'someVal'
`)

			if err := yaml.Unmarshal(mockConfig, &c); err != nil {
				panic(err)
			}
			So(producerIsConfigured("http", c), ShouldBeTrue)
			So(producerIsConfigured("kafka", c), ShouldBeTrue)
			So(producerIsConfigured("statsd", c), ShouldBeTrue)
		})
		Convey("Should return false if a producer configuration wasn't provided", func() {
			var c Config
			mockConfig := []byte(`
---
producers:
    http:
        someConfig: 'someVal'
    statsd:
        someConfig: 'someVal'
`)

			if err := yaml.Unmarshal(mockConfig, &c); err != nil {
				panic(err)
			}
			So(producerIsConfigured("someBogusProducer", c), ShouldBeFalse)
		})
	})
}
