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
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"testing"

	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/urfave/cli"
)

var (
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
	updatedFooBarMetric = producers.MetricsMessage{
		Datapoints: []producers.Datapoint{
			producers.Datapoint{
				Name:      "foo.bar",
				Value:     456,
				Unit:      "",
				Timestamp: "2010-01-02T00:01:02.000000003Z",
			},
			producers.Datapoint{
				Name:      "foo.qux",
				Value:     123.5,
				Unit:      "",
				Timestamp: "2010-01-02T00:01:02.000000003Z",
			},
		},
	}
	fooBarMetricWithDimensions = producers.MetricsMessage{
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
				Tags: map[string]string{
					"frodo":   "baggins",
					"samwise": "gamgee",
				},
			},
		},
		Dimensions: producers.Dimensions{
			TaskID: "task-id-here",
		},
	}
)

func TestConversion(t *testing.T) {
	Convey("When converting metrics", t, func() {
		text := messageToPromText(fooBarMetric)
		So(text, ShouldContainSubstring, "foo_bar 123 1262390462000")
		So(text, ShouldContainSubstring, "foo_baz 123.5 1262390462000")
	})

	Convey("When converting metrics with dimensions", t, func() {
		text := messageToPromText(fooBarMetricWithDimensions)
		So(text, ShouldContainSubstring, "foo_bar(task_id:\"task-id-here\") 123 1262390462000")
		So(text, ShouldContainSubstring, "foo_baz(task_id:\"task-id-here\",frodo:\"baggins\",samwise:\"gamgee\") 123.5 1262390462000")
	})
}

func TestPromPlugin(t *testing.T) {
	app := cli.NewApp()
	app.Flags = pluginFlags
	app.Before = startPromServer
	app.After = stopPromServer

	Convey("When supplying metrics to the connector", t, func() {
		app.Action = func(c *cli.Context) error {
			port := c.Int("prometheus-port")

			Convey("When the plugin has started", func() {
				response, status := getLocalMetrics(port)
				So(status, ShouldEqual, http.StatusNoContent)
				So(response, ShouldBeEmpty)
			})

			Convey("When the plugin is supplied with metrics", func() {
				// Supply foo.bar and foo.baz to the plugin
				err := promConnector([]producers.MetricsMessage{fooBarMetric}, c)
				So(err, ShouldBeNil)

				response, status := getLocalMetrics(port)
				So(status, ShouldEqual, http.StatusOK)
				So(response, ShouldContainSubstring, "foo_bar")
				So(response, ShouldContainSubstring, "foo_baz")

				// Supply foo.bar and baz.qux to the plugin
				err = promConnector([]producers.MetricsMessage{updatedFooBarMetric}, c)
				So(err, ShouldBeNil)

				response, status = getLocalMetrics(port)
				So(status, ShouldEqual, http.StatusOK)
				// The original metric is still present
				So(response, ShouldContainSubstring, "foo_bar")
				// The replaced metric is not present
				So(response, ShouldNotContainSubstring, "foo_baz")
				// The new metric is present
				So(response, ShouldContainSubstring, "foo_qux")

				// Supply multiple messages to the plugin
				err = promConnector([]producers.MetricsMessage{fooBarMetric, updatedFooBarMetric}, c)
				So(err, ShouldBeNil)

				response, status = getLocalMetrics(port)
				So(status, ShouldEqual, http.StatusOK)
				// All metrics should be present
				So(response, ShouldContainSubstring, "foo_bar")
				So(response, ShouldContainSubstring, "foo_baz")
				So(response, ShouldContainSubstring, "foo_qux")
			})

			return nil

		}
		app.Run([]string{"", "-prometheus-port", freeport()})
	})
}

// freeport listens momentarily on port 0, then closes the connection and
// returns the assigned port, which is now known to be available.
func freeport() string {
	l, err := net.Listen("tcp", ":0")
	defer l.Close()
	if err != nil {
		panic(err)
	}
	_, p, _ := net.SplitHostPort(l.Addr().String())
	return p
}

// getLocalMetrics fetches text from http://localhost:port/metrics
func getLocalMetrics(port int) (string, int) {
	addr := fmt.Sprintf("http://localhost:%d/metrics", port)
	response, err := http.Get(addr)
	if err != nil {
		panic(err)
	}
	defer response.Body.Close()
	body, err := ioutil.ReadAll(response.Body)
	if err != nil {
		panic(err)
	}
	return string(body), response.StatusCode
}
