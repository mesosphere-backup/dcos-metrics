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
	"net/http"
	"testing"

	. "github.com/smartystreets/goconvey/convey"
	"github.com/urfave/cli"
)

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
			return nil

		}
		app.Run([]string{"", "-prometheus-port", "8080"})
	})
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
