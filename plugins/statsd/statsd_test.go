// Copyright 2017 Mesosphere, Inc.
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
	"math"
	"net"
	"testing"

	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/urfave/cli"
)

var (
	// A message with a single `foo.bar` metric
	fooBarMetric = producers.MetricsMessage{
		Datapoints: []producers.Datapoint{
			producers.Datapoint{
				Name:      "foo.bar",
				Value:     123,
				Unit:      "",
				Timestamp: "2010-01-02T00:01:02.000000003Z",
			},
			producers.Datapoint{
				Name:      "foo.baz",
				Value:     123.5,
				Unit:      "",
				Timestamp: "2010-01-02T00:01:02.000000003Z",
			},
		},
	}
)

// mockStatsdServer listens on localhost with a random free local port,
// returning its port, a channel for coms, and its connection
func mockStatsdServer() (string, chan string, *net.UDPConn) {
	msg := make(chan string)

	addr := net.UDPAddr{
		Port: 0,
		IP:   net.ParseIP("127.0.0.1"),
	}

	conn, err := net.ListenUDP("udp", &addr)
	if err != nil {
		panic(err)
	}

	go func() {
		// Listen for UDP packets and relay them to the msg channel
		for {
			buffer := make([]byte, 1024)
			n, _, err := conn.ReadFromUDP(buffer)
			if err != nil {
				// Connection was closed
				break
			}
			message := string(buffer[0:n])
			msg <- message
		}
	}()

	_, p, _ := net.SplitHostPort(conn.LocalAddr().String())
	return p, msg, conn
}

func TestDatapointConverter(t *testing.T) {
	Convey("When converting a datapoint", t, func() {
		name, val, ok := convertDatapointToStatsd(fooBarMetric.Datapoints[0])
		Convey("It should report success", func() {
			So(ok, ShouldBeTrue)
		})
		Convey("It should contain the metric name", func() {
			So(name, ShouldEqual, "foo.bar")
		})
		Convey("It should pass in the metric value", func() {
			So(val, ShouldStartWith, "123")
		})
		Convey("It should always be a gauge", func() {
			So(val, ShouldEndWith, "g")
		})
		Convey("It should round to the nearest integer", func() {
			_, val, _ = convertDatapointToStatsd(fooBarMetric.Datapoints[1])
			So(val, ShouldEqual, "124|g")
		})
	})
}

func TestStatsdConnector(t *testing.T) {
	// Start a UDP Server which accepts anything
	port, msgs, conn := mockStatsdServer()
	defer conn.Close()

	// This closely follows how the plugin is constructed.
	app := cli.NewApp()
	app.Flags = pluginFlags

	Convey("When supplying metrics to the connector", t, func() {
		app.Action = func(c *cli.Context) {
			// Inside app.Action, the cli context is appropriate populated
			err := statsdConnector([]producers.MetricsMessage{fooBarMetric}, c)
			So(err, ShouldBeNil)
			metric := <-msgs
			So(metric, ShouldEqual, "foo.bar:123|g")
		}
		// Pass the appropriate configuration into the plugin
		app.Run([]string{"", "--statsd-udp-host", "127.0.0.1", "--statsd-udp-port", port})
	})
}

func TestNormalize(t *testing.T) {
	tests := map[interface{}]int{
		123:             123,
		int32(123):      123,
		int64(123):      123,
		float32(123.45): 123,
		float64(123.45): 123,
		123.54:          124,
		"123":           123,
		"123.45":        123,
		"123.54":        124,
	}
	errors := []interface{}{
		"NaN",
		math.NaN(),
	}
	Convey("Inputs are cast and rounded", t, func() {
		for input, expected := range tests {
			val, err := normalize(input)
			So(err, ShouldBeNil)
			So(val, ShouldEqual, expected)
		}
		for _, input := range errors {
			output, err := normalize(input)
			So(output, ShouldEqual, -1)
			So(err, ShouldNotBeNil)
		}
	})
}
