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
	"math"
	"strconv"

	log "github.com/Sirupsen/logrus"
	plugin "github.com/dcos/dcos-metrics/plugins"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/etsy/statsd/examples/go"
	"github.com/urfave/cli"
)

var (
	pluginFlags = []cli.Flag{
		cli.StringFlag{
			Name:   "statsd-udp-host",
			Usage:  "The address of the statsd server",
			EnvVar: "STATSD_UDP_HOST",
			Value:  "127.0.0.1",
		},
		cli.IntFlag{
			Name:   "statsd-udp-port",
			Usage:  "The statsdPort of the statsd server",
			EnvVar: "STATSD_UDP_PORT",
			Value:  8125,
		},
	}
	statsdClient *statsd.StatsdClient
)

func main() {
	log.Info("Starting statsd DC/OS metrics plugin")

	statsdPlugin, err := plugin.New(
		plugin.Name("statsd"),
		plugin.ExtraFlags(pluginFlags),
		plugin.ConnectorFunc(statsdConnector))

	if err != nil {
		log.Fatal(err)
	}

	log.Fatal(statsdPlugin.StartPlugin())
}

// statsdConnector is the method called by the plugin every time it retrieves
// metrics from the API.
func statsdConnector(metrics []producers.MetricsMessage, c *cli.Context) error {
	if statsdClient == nil {
		statsdHost := c.String("statsd-udp-host")
		statsdPort := c.Int("statsd-udp-port")

		log.Infof("Setting up new statsd client %s:%d", statsdHost, statsdPort)
		statsdClient = statsd.New(statsdHost, statsdPort)
	}

	if len(metrics) == 0 {
		log.Info("No messages received from metrics service")
		return nil
	}
	for _, message := range metrics {
		log.Debugf("Sending %d datapoints to statsd server...", len(message.Datapoints))
		emitDatapointsOverStatsd(message.Datapoints, statsdClient)
	}

	return nil
}

// emitDatapointsOverStatsd converts datapoints to statsd format and dispatches
// them in a batch to the statsd server
func emitDatapointsOverStatsd(datapoints []producers.Datapoint, client *statsd.StatsdClient) error {
	data := make(map[string]string)
	for _, dp := range datapoints {
		name, val, ok := convertDatapointToStatsd(dp)
		// we silently drop metrics which could not be converted
		if ok {
			data[name] = val
		}
	}
	client.Send(data, 1)
	return nil
}

// convertDatapointToStatsd attempts to convert a datapoint to a statsd format
// name + value, returning a false ok flag if the conversion failed.
func convertDatapointToStatsd(datapoint producers.Datapoint) (string, string, bool) {
	// Value is of type interface{}, hence sprintf
	val, err := normalize(datapoint.Value)
	if err != nil {
		// This is only debug-level because we expect many NaNs in regular usage
		log.Debugf("Metric %s failed to convert: %q", datapoint.Name, err)
		return "", "", false
	}
	return datapoint.Name, fmt.Sprintf("%d|g", val), true
}

// normalize converts ints, floats and strings to rounded ints
// It errors on other types and NaNs.
func normalize(i interface{}) (int, error) {
	switch v := i.(type) {
	case int:
		return v, nil
	case float64:
		// We need this check here because NaN can be a float. Casting NaN to
		// int results in a large negative number. Even after you add 0.5.
		if math.IsNaN(v) {
			return -1, fmt.Errorf("Could not normalize NaN %q", v)
		}
		return int(v + 0.5), nil
	case string:
		f, err := strconv.ParseFloat(v, 32)
		if err != nil {
			return -1, err
		}
		return normalize(f)
	default:
		return -1, fmt.Errorf("Could not normalize %q", v)
	}
}
