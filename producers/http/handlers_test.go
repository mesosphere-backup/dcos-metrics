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
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"strconv"
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
				Timestamp: testTime.UTC().Format(time.RFC3339),
			},
		},
		Dimensions: producers.Dimensions{
			MesosID:     "foo",
			Hostname:    "some-host",
			ContainerID: "foo-container",
		},
		Timestamp: testTime.UTC().Unix(),
	}

	allTestData = []producers.MetricsMessage{
		testNodeData,
		testContainerData,
		testAppData,
	}
)

func setup(messages []producers.MetricsMessage) int {
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

	for _, m := range messages {
		pc <- m
	}

	return port
}

func TestNodeHandler(t *testing.T) {
	Convey("When querying the /v0/node endpoint", t, func() {
		Convey("Should return metrics in the expected structure", func() {
			port := setup(allTestData)
			resp, err := http.Get(urlBuilder("localhost", port, "/v0/node"))
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

			So(resp.StatusCode, ShouldEqual, http.StatusOK)
			So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
		})
	})
}

func TestContainersHandler(t *testing.T) {
	Convey("When querying the /v0/containers endpoint", t, func() {
		Convey("Should return container IDs in the expected structure", func() {
			port := setup(allTestData)
			resp, err := http.Get(urlBuilder("localhost", port, "/v0/containers"))
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

			So(resp.StatusCode, ShouldEqual, http.StatusOK)
			So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
		})
		Convey("Should return container IDs when app metrics, but not container metrics, are present", func() {
			port := setup([]producers.MetricsMessage{testNodeData, testAppData})
			resp, err := http.Get(urlBuilder("localhost", port, "/v0/containers"))
			if err != nil {
				panic(err)
			}
			defer resp.Body.Close()

			got, err := ioutil.ReadAll(resp.Body)
			if err != nil {
				panic(err)
			}
			// Note that we didn't supply testContainerData in `setup` above.
			expected, err := json.Marshal([]string{testContainerData.Dimensions.ContainerID})
			if err != nil {
				panic(err)
			}

			So(resp.StatusCode, ShouldEqual, http.StatusOK)
			So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
		})
	})
}

func TestContainerHandler(t *testing.T) {
	Convey("When querying the /v0/containers/{id} endpoint", t, func() {
		Convey("Should return container metrics for the container ID given", func() {
			port := setup(allTestData)
			resp, err := http.Get(urlBuilder("localhost", port, "/v0/containers/foo-container"))
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

			So(resp.StatusCode, ShouldEqual, http.StatusOK)
			So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
		})
	})
}

func TestContainerAppHandler(t *testing.T) {
	Convey("When querying the /v0/containers/{id}/app endpoint", t, func() {
		Convey("Should return app metrics in the expected structure", func() {
			port := setup(allTestData)
			resp, err := http.Get(urlBuilder("localhost", port, "/v0/containers/foo-container/app"))
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

			So(resp.StatusCode, ShouldEqual, http.StatusOK)
			So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
		})
	})
}

func TestContainerAppMetricHandler(t *testing.T) {
	Convey("When querying the /v0/containers/{id}/app/{metric-id} endpoint", t, func() {
		Convey("Should return app metrics in the expected structure", func() {
			port := setup(allTestData)
			resp, err := http.Get(urlBuilder("localhost", port, "/v0/containers/foo-container/app/some-app-metric"))
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

			So(resp.StatusCode, ShouldEqual, http.StatusOK)
			So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
		})
	})
}

func TestPingHandler(t *testing.T) {
	Convey("When querying the /v0/ping endpoint", t, func() {
		Convey("Should return a message and a timestamp", func() {
			port := setup(allTestData)
			resp, err := http.Get(urlBuilder("localhost", port, "/v0/ping"))
			if err != nil {
				panic(err)
			}
			defer resp.Body.Close()

			got, err := ioutil.ReadAll(resp.Body)
			if err != nil {
				panic(err)
			}
			expected, err := json.Marshal(struct {
				OK        bool   `json:"ok"`
				Timestamp string `json:"timestamp"`
			}{
				OK:        true,
				Timestamp: time.Now().UTC().Format(time.RFC3339),
			})
			if err != nil {
				panic(err)
			}

			So(resp.StatusCode, ShouldEqual, http.StatusOK)
			So(strings.TrimSpace(string(got)), ShouldEqual, strings.TrimSpace(string(expected)))
		})
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

func urlBuilder(host string, port int, endpoint string) string {
	u := url.URL{
		Scheme: "http",
		Host:   net.JoinHostPort(host, strconv.Itoa(port)),
		Path:   endpoint,
	}
	return u.String()
}
