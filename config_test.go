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
	"context"
	"errors"
	"flag"
	"io/ioutil"
	"net"
	"net/url"
	"os"
	"testing"
	"time"

	"strings"

	"github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/dcos/nodeutil"
	. "github.com/smartystreets/goconvey/convey"
)

type fakeInfo struct {
	ip           net.IP
	ipErr        error
	leader       bool
	leaderErr    error
	mesosID      string
	mesosIDErr   error
	clusterID    string
	clusterIDErr error
}

var _ nodeutil.NodeInfo = &fakeInfo{}

func (f *fakeInfo) DetectIP() (net.IP, error) {
	return f.ip, f.ipErr
}

func (f *fakeInfo) IsLeader() (bool, error) {
	return f.leader, f.leaderErr
}

func (f *fakeInfo) MesosID(ctx context.Context) (string, error) {
	return f.mesosID, f.mesosIDErr
}

func (f *fakeInfo) ClusterID() (string, error) {
	return f.clusterID, f.clusterIDErr
}

func TestGetNodeInfo(t *testing.T) {
	Convey("When getting node info", t, func() {

		var fetchedURLs []string
		testConfig, _ := getNewConfig([]string{"-role", "agent"})

		testConfig.NodeInfoFunc = func(url url.URL) (nodeutil.NodeInfo, error) {
			fetchedURLs = append(fetchedURLs, url.String())
			info := &fakeInfo{
				ip:        net.ParseIP("127.0.0.1"),
				clusterID: "my-cluster-ID",
			}
			if strings.HasPrefix(url.String(), "https://") {
				info.mesosIDErr = errors.New("Can't get mesos ID over http")
			} else {
				info.mesosID = "my mesos id"
			}
			return info, nil
		}

		Convey("When an IAM Config  is set", func() {
			testConfig.IAMConfigPath = "some config path"
			testConfig.getNodeInfo(true)

			So(len(fetchedURLs), ShouldEqual, 2)
			So(fetchedURLs[0], ShouldStartWith, "https://")
			So(fetchedURLs[1], ShouldStartWith, "http://")
		})

		Convey("When an IAM Config is not set", func() {
			// don't set an IAM config path here
			testConfig.getNodeInfo(true)

			So(len(fetchedURLs), ShouldEqual, 1)
			So(fetchedURLs[0], ShouldStartWith, "http")
		})

	})
}

func TestNewConfig(t *testing.T) {
	Convey("Ensure default configuration is set properly", t, func() {
		testConfig := newConfig()

		Convey("Default node polling period should be 60 seconds", func() {
			So(testConfig.Collector.Node.PollPeriod, ShouldEqual, 60*time.Second)
		})

		Convey("Default mesos agent polling period should be 60 seconds", func() {
			So(testConfig.Collector.MesosAgent.PollPeriod, ShouldEqual, 60*time.Second)
		})

		Convey("HTTP profiler should be disabled by default", func() {
			So(testConfig.Collector.HTTPProfiler, ShouldBeFalse)
		})

		Convey("Default log level should be 'info'", func() {
			So(testConfig.LogLevel, ShouldEqual, "info")
		})

		Convey("Default HTTP producer port should be 9000", func() {
			So(testConfig.Producers.HTTPProducerConfig.Port, ShouldEqual, 9000)
		})

		Convey("Default cache expiry should be 2 minutes", func() {
			So(testConfig.Producers.HTTPProducerConfig.CacheExpiry, ShouldEqual, 120*time.Second)
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
	tmpConfig, teardown := createMockConfigFile([]byte(`
---
collector:
  mesos_agent:
    port: 1234
    poll_period: 5
    request_protocol: https
  node:
    poll_period: 3
  http_profiler: false
`))
	defer teardown()

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

func TestGetNewConfig(t *testing.T) {
	// Mock out and create the config file
	tmpConfig, teardown := createMockConfigFile([]byte(`
---
collector:
  mesos_agent:
    poll_period: 90s
`))
	defer teardown()
	testConfig, _ := getNewConfig([]string{"-role", "agent", "-config", tmpConfig.Name()})

	Convey("When getting the service configuration", t, func() {
		Convey("Should error if the user did not specify exactly one role (master or agent)", func() {
			Convey("If the role flag is missing", func() {
				_, err := getNewConfig([]string{""})
				So(err, ShouldNotBeNil)
			})
			Convey("If the provided value is not a valid role", func() {
				_, err := getNewConfig([]string{"-role", "foo"})
				So(err, ShouldNotBeNil)
			})
		})
		Convey("Should ensure that the cache expiry is not less than twice the poll period", func() {
			// Note that this returns an error in environments which are missing the
			// /opt/mesosphere/bin/detect_ip script, eg CI
			// TODO(philip) mock calls to FileDetctIP
			So(testConfig.Producers.HTTPProducerConfig.CacheExpiry, ShouldEqual, 180*time.Second)
		})

		Convey("Should use all defaults if the -config flag wasn't passed", func() {
			// I really don't like ignoring the err here, but unfortunately we
			// have no choice: chances are good that "/opt/mesosphere/bin/detect_ip"
			// doesn't exist on your system. Since we use nodeutil from dcos-go, we
			// can't mock the path to the IP detect script here. So err is always
			// not-nil in this test :'(   -- roger, 2016-12-05
			c, _ := getNewConfig([]string{"-role", "agent"})
			So(c.Collector, ShouldResemble, newConfig().Collector)
			So(c.Producers, ShouldResemble, newConfig().Producers)
			So(c.LogLevel, ShouldResemble, newConfig().LogLevel)
		})
	})
}

// makeTempConfigFile writes the specified file contents to a temporary file,
// returning a pointer to the file and a teardown method which deletes it
func createMockConfigFile(contents []byte) (*os.File, func()) {
	// Mock out and create the config file
	tmpConfig, err := ioutil.TempFile("", "testConfig")
	if err != nil {
		panic(err)
	}

	if _, err := tmpConfig.Write(contents); err != nil {
		panic(err)
	}

	return tmpConfig, func() {
		os.Remove(tmpConfig.Name())
	}
}
