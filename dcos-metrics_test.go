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

	"github.com/Sirupsen/logrus"
	. "github.com/smartystreets/goconvey/convey"
)

func TestMain(t *testing.T) {
	Convey("Ensure collector loads and responds", t, nil)
}

func TestDefaultConfig(t *testing.T) {
	Convey("Ensure default configuration is set properly", t, func() {
		testConfig := newConfig()

		Convey("Default node polling period should be 15", func() {
			So(testConfig.Collector.Node.PollPeriod, ShouldEqual, 15)
		})

		Convey("Default mesos agent polling period should be 15", func() {
			So(testConfig.Collector.MesosAgent.PollPeriod, ShouldEqual, 15)
		})

		Convey("HTTP profiler should be enabled by default", func() {
			So(testConfig.Collector.HTTPProfiler, ShouldBeTrue)
		})

		Convey("Default log level should be 'info'", func() {
			So(testConfig.LogLevel, ShouldEqual, "info")
		})
	})
}

func TestSetFlags(t *testing.T) {
	Convey("When command line arguments are provided", t, func() {
		Convey("Should apply an alternate configuration path", func() {
			testConfig := Config{
				ConfigPath: "/some/default/path",
			}
			testFS := flag.NewFlagSet("", flag.PanicOnError)
			testConfig.setFlags(testFS)
			testFS.Parse([]string{"-config", "/another/config/path"})

			So(testConfig.ConfigPath, ShouldEqual, "/another/config/path")
		})

		Convey("Should apply an alternate log level", func() {
			testConfig := Config{
				LogLevel: "debug",
			}
			testFS := flag.NewFlagSet("", flag.PanicOnError)
			testConfig.setFlags(testFS)
			testFS.Parse([]string{"-loglevel", "debug"})

			lvl, err := logrus.ParseLevel(testConfig.LogLevel)
			if err != nil {
				panic(err)
			}

			So(testConfig.LogLevel, ShouldEqual, "debug")
			So(lvl, ShouldEqual, logrus.DebugLevel)
		})
	})
}

func TestLoadConfig(t *testing.T) {
	// Mock out and create the config file
	configContents := []byte(`
---
collector:
  mesos_agent:
    port: 1234
    poll_period: 5
    request_protocol: https
  node:
    poll_period: 3
  http_profiler: false
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

			So(testConfig.Collector.MesosAgent.Port, ShouldEqual, 1234)
			So(testConfig.Collector.MesosAgent.PollPeriod, ShouldEqual, 5)
			So(testConfig.Collector.Node.PollPeriod, ShouldEqual, 3)
			So(testConfig.Collector.HTTPProfiler, ShouldBeFalse)
			So(testConfig.Collector.MesosAgent.RequestProtocol, ShouldEqual, "https")
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
`)

			if err := yaml.Unmarshal(mockConfig, &c); err != nil {
				panic(err)
			}
			So(producerIsConfigured("http", c), ShouldBeTrue)
		})
		Convey("Should return false if a producer configuration wasn't provided", func() {
			var c Config
			mockConfig := []byte(`
---
producers:
    http:
        someConfig: 'someVal'
`)

			if err := yaml.Unmarshal(mockConfig, &c); err != nil {
				panic(err)
			}
			So(producerIsConfigured("someBogusProducer", c), ShouldBeFalse)
		})
	})
}
