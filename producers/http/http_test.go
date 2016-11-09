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

// Functional test for the /api/v0/agent endpoint.
func TestHTTPProducer_Agent(t *testing.T) {
	testTime := time.Now()

	testData := []producers.MetricsMessage{
		producers.MetricsMessage{
			Name: strings.Join([]string{producers.AgentMetricPrefix, "foo"}, producers.MetricNamespaceSep),
			Datapoints: []producers.Datapoint{
				producers.Datapoint{
					Name: strings.Join([]string{
						producers.AgentMetricPrefix,
						"foo",
						"some-metric",
					}, producers.MetricNamespaceSep),
					Unit:      "",
					Value:     "1234",
					Timestamp: testTime.Format(time.RFC3339Nano),
				},
			},
			Dimensions: producers.Dimensions{
				AgentID:  "foo",
				Hostname: "some-host",
			},
			Timestamp: testTime.UTC().Unix(),
		},
	}

	port, err := getEphemeralPort()
	if err != nil {
		panic(err)
	}

	Convey("When querying the /api/v0/agent endpoint", t, func() {
		Convey("Should return metrics in the expected structure", func() {
			pi, pc := New(Config{IP: "127.0.0.1", Port: port, CacheExpiry: time.Duration(5) * time.Second})
			go pi.Run()
			time.Sleep(1 * time.Second) // give the http server a chance to start before querying it

			for _, td := range testData {
				pc <- td
			}

			resp, err := http.Get(fmt.Sprintf("http://127.0.0.1:%d", port))
			if err != nil {
				panic(err)
			}
			defer resp.Body.Close()

			b, err := ioutil.ReadAll(resp.Body)
			fmt.Println(b)
		})
	})
}

// Functional test for the /api/v0/containers endpoint.
func TestHTTPProducer_Containers(t *testing.T) {
	Convey("When querying the /api/v0/containers endpoint", t, func() {
		Convey("Should return metrics in the expected structure", nil)
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
