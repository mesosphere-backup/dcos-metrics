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

// Functional test for the /api/v0/agent endpoint.
func TestHTTPProducer_Agent(t *testing.T) {
	testTime := time.Now()

	testData := producers.MetricsMessage{
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

			pc <- testData
			resp, err := http.Get(fmt.Sprintf("http://127.0.0.1:%d/api/v0/agent", port))
			if err != nil {
				panic(err)
			}
			defer resp.Body.Close()

			got, err := ioutil.ReadAll(resp.Body)
			if err != nil {
				panic(err)
			}
			expected, err := json.Marshal(testData)
			if err != nil {
				panic(err)
			}

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
