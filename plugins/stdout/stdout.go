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
	log "github.com/Sirupsen/logrus"
	plugin "github.com/dcos/dcos-metrics/plugins"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/urfave/cli"
)

func main() {
	log.Info("Starting stdout DC/OS metrics plugin")
	log.Info("This plugin is an example; you probably shouldn't be running it in production.")

	// plugin.New initites the plugin. Here we pass only minimal options.
	stdoutPlugin, err := plugin.New(
		plugin.Name("stdout"),
		plugin.ConnectorFunc(stdoutConnector))

	if err != nil {
		log.Fatal(err)
	}

	log.Fatal(stdoutPlugin.StartPlugin())
}

// stdoutConnector will be called each time a new set of metrics messages
// is received. It simply prints each message.
func stdoutConnector(metrics []producers.MetricsMessage, c *cli.Context) error {
	if len(metrics) == 0 {
		log.Info("No messages received from metrics service")
		return nil
	}
	for _, message := range metrics {
		dimensions := &message.Dimensions

		// we print a few potentially interesting dimensions
		log.Infof("Metrics from task %s, framework %s, host %s", dimensions.ExecutorID, dimensions.FrameworkName, dimensions.Hostname)
		if len(dimensions.Labels) > 0 {
			log.Infof("Labels: %v", dimensions.Labels)
		}

		log.Info("Datapoints:")
		for _, datapoint := range message.Datapoints {
			// For clarity, we don't print timestamp or tags here.
			log.Infof("  %s: %v", datapoint.Name, datapoint.Value)
		}
	}

	return nil
}
