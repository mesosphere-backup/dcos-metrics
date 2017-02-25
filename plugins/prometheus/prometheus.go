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
	"net/http"
	"os"

	"github.com/Sirupsen/logrus"
	plugin "github.com/dcos/dcos-metrics/plugins"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
	"github.com/urfave/cli"
)

func main() {
	proFlags := []cli.Flag{
		cli.StringFlag{
			Name:  "port",
			Value: ":8010",
			Usage: "Port for exposing metrics to Prometheus",
		},
	}

	proPlugin, err := plugin.New(proFlags)
	if err != nil {
		logrus.Fatal(err)
	}

	proPlugin.App.Action = func(c *cli.Context) {

		go func() {
			for {
				metrics, err := proPlugin.Metrics()
				if err != nil {
					logrus.Fatal(err)
				}

				for _, msg := range metrics {
					for _, dp := range msg.Datapoints {
						dpGauge := prometheus.NewGauge(prometheus.GaugeOpts{
							Name: dp.Name,
							Help: "DC/OS metric for " + dp.Name,
						})

						dpGauge.Set(dp.Value.(float64))
					}
				}
			}
		}()

		// Expose the registered metrics via HTTP.
		http.Handle("/metrics", promhttp.Handler())
		logrus.Fatal(http.ListenAndServe(c.String("port"), nil))
	}

	proPlugin.App.Run(os.Args)
}
