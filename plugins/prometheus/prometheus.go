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
	"net"
	"net/http"
	"sync"

	log "github.com/Sirupsen/logrus"
	plugin "github.com/dcos/dcos-metrics/plugins"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/urfave/cli"
)

var (
	pluginFlags = []cli.Flag{
		cli.IntFlag{
			Name:   "prometheus-port",
			Usage:  "The port on which to serve prometheus metrics",
			EnvVar: "PROMETHEUS_PORT",
			Value:  8080,
		},
	}
	registerOnce sync.Once
	listener     net.Listener
)

func main() {
	log.Info("Starting statsd DC/OS metrics plugin")

	promPlugin, err := plugin.New(
		plugin.Name("prometheus"),
		plugin.ExtraFlags(pluginFlags),
		plugin.ConnectorFunc(promConnector))

	promPlugin.BeforeFunc = startPromServer
	promPlugin.AfterFunc = stopPromServer

	if err != nil {
		log.Fatal(err)
	}

	log.Fatal(promPlugin.StartPlugin())
}

func startPromServer(c *cli.Context) error {
	addr := fmt.Sprintf(":%d", c.Int("prometheus-port"))
	log.Infof("Starting Prometheus server on localhost%s", addr)

	registerOnce.Do(func() {
		// the server may start and stop, but the handler must only be
		// registered once
		http.HandleFunc("/metrics", serveMetrics)
	})

	// err is declared to avoid listener being locally scoped
	var err error
	listener, err = net.Listen("tcp", addr)
	if err != nil {
		return err
	}

	go func() {
		err = http.Serve(listener, nil)
	}()

	return err
}

func stopPromServer(c *cli.Context) error {
	log.Info("Halting Prometheus server.")
	listener.Close()
	return nil
}

func promConnector(metrics []producers.MetricsMessage, c *cli.Context) error {
	return nil
}

func serveMetrics(w http.ResponseWriter, r *http.Request) {
	http.Error(w, "", http.StatusNoContent)
}
