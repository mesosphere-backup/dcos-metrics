//+build unit

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

package http

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"strings"
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

var (
	testTime = time.Now()

	testNodeData = producers.MetricsMessage{
		Name: producers.NodeMetricPrefix,
		Datapoints: []producers.Datapoint{
			producers.Datapoint{
				Name:      "some-node-metric",
				Unit:      "",
				Value:     "1234",
				Timestamp: testTime.Format(time.RFC3339Nano),
			},
		},
		Dimensions: producers.Dimensions{
			MesosID:  "foo",
			Hostname: "some-host",
		},
		Timestamp: testTime.UTC().Unix(),
	}

	testContainerData = producers.MetricsMessage{
		Name: producers.ContainerMetricPrefix,
		Datapoints: []producers.Datapoint{
			producers.Datapoint{
				Name:      "some-container-metric",
				Unit:      "",
				Value:     "1234",
				Timestamp: testTime.Format(time.RFC3339Nano),
			},
		},
		Dimensions: producers.Dimensions{
			MesosID:     "foo",
			Hostname:    "some-host",
			ContainerID: "foo-container",
		},
		Timestamp: testTime.UTC().Unix(),
	}

	testAppData = producers.MetricsMessage{
		Name: producers.AppMetricPrefix,
		Datapoints: []producers.Datapoint{
			producers.Datapoint{
				Name:      "some-app-metric",
				Unit:      "",
				Value:     "1234",
				Timestamp: testTime.Format(time.RFC3339Nano),
			},
		},
		Dimensions: producers.Dimensions{
			MesosID:     "foo",
			Hostname:    "some-host",
			ContainerID: "foo-container",
		},
		Timestamp: testTime.UTC().Unix(),
	}
)

func setup() int {
	port, err := getEphemeralPort()
	if err != nil {
		panic(err)
	}

	pi, pc := New(Config{
		DCOSRole:    "agent",
		IP:          "127.0.0.1",
		Port:        port,
		CacheExpiry: time.Duration(5) * time.Second})
	go pi.Run()
	time.Sleep(1 * time.Second) // give the http server a chance to start before querying it

	pc <- testNodeData
	pc <- testContainerData
	pc <- testAppData

	return port
}

// Functional test for the /system/metrics/api/v0/node endpoint.
func TestHTTPProducer_Node(t *testing.T) {
	Convey("When querying the /system/metrics/api/v0/node endpoint", t, func() {
		Convey("Should return metrics in the expected structure", func() {
			port := setup()
			resp, err := http.Get(fmt.Sprintf("http://127.0.0.1:%d/system/metrics/api/v0/node", port))
			if err != nil {
				panic(err)
			}
			defer resp.Body.Close()

			got, err := ioutil.ReadAll(resp.Body)
			if err != nil {
				panic(err)
			}

			expected, err := json.Marshal(testNodeData)
			if err != nil {
				panic(err)
			}

			So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
		})
	})
}

func TestHTTPProducer_Containers(t *testing.T) {
	Convey("When querying the /system/metrics/api/v0/containers endpoint", t, func() {
		Convey("Should return container IDs in the expected structure", nil)
		port := setup()
		resp, err := http.Get(fmt.Sprintf("http://127.0.0.1:%d/system/metrics/api/v0/containers", port))
		if err != nil {
			panic(err)
		}
		defer resp.Body.Close()

		got, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			panic(err)
		}
		expected, err := json.Marshal([]string{testContainerData.Dimensions.ContainerID})
		if err != nil {
			panic(err)
		}

		So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
	})
}

func TestHTTPProducer_ContainerID(t *testing.T) {
	Convey("When querying the /system/metrics/api/v0/containers/{id} endpoint", t, func() {
		Convey("Should return container metrics for the container ID given", nil)
		port := setup()
		resp, err := http.Get(fmt.Sprintf("http://127.0.0.1:%d/system/metrics/api/v0/containers/foo-container", port))
		if err != nil {
			panic(err)
		}
		defer resp.Body.Close()

		got, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			panic(err)
		}
		expected, err := json.Marshal(testContainerData)
		if err != nil {
			panic(err)
		}

		So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
	})
}

func TestHTTPProducer_ContainerApp(t *testing.T) {
	Convey("When querying the /system/metrics/api/v0/containers/{id}/app endpoint", t, func() {
		Convey("Should return app metrics in the expected structure", nil)
		port := setup()
		resp, err := http.Get(fmt.Sprintf("http://127.0.0.1:%d/system/metrics/api/v0/containers/foo-container/app", port))
		if err != nil {
			panic(err)
		}
		defer resp.Body.Close()

		got, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			panic(err)
		}
		expected, err := json.Marshal(testAppData)
		if err != nil {
			panic(err)
		}

		So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
	})
}

// getEphemeralPort returns an available ephemeral port on the system.
func getEphemeralPort() (int, error) {
	addr, err := net.ResolveTCPAddr("tcp", "localhost:0")
	if err != nil {
		return 0, err
	}

	l, err := net.ListenTCP("tcp", addr)
	if err != nil {
		return 0, err
	}
	defer l.Close()

	return l.Addr().(*net.TCPAddr).Port, nil
}
