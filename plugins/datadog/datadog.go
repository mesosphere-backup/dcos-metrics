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
	"errors"
	"fmt"
	"net"
	"reflect"
	"strings"

	"github.com/DataDog/datadog-go/statsd"
	log "github.com/sirupsen/logrus"
	"github.com/urfave/cli"

	plugin "github.com/dcos/dcos-metrics/plugins"
	"github.com/dcos/dcos-metrics/producers"
)

var (
	datadogHost   = "datadog-agent.marathon.mesos"
	datadogPort   = "8125"
	ddPluginFlags = []cli.Flag{
		cli.StringFlag{
			Name:  "datadog-host",
			Value: datadogHost,
			Usage: "Datadog output hostname",
		},
		cli.StringFlag{
			Name:  "datadog-port",
			Value: datadogPort,
			Usage: "Datadog output UDP port",
		},
	}
	datadogConnector = func(metrics []producers.MetricsMessage, c *cli.Context) error {
		if len(metrics) == 0 {
			log.Error("No messages received from metrics service")
		} else {

			for _, m := range metrics {
				log.Info("Sending metrics to datadog...")
				datadogAgent := net.JoinHostPort(c.String("datadog-host"), c.String("datadog-port"))

				if err := toStats(m, datadogAgent); err != nil {
					log.Errorf("Errors encountered sending metrics to Datadog: %s", err.Error())
					continue
				}
			}
		}
		return nil
	}
)

func main() {
	log.Info("Starting datadog DC/OS metrics plugin")

	datadogPlugin, err := plugin.New(
		plugin.Name("datadog"),
		plugin.ExtraFlags(ddPluginFlags),
		plugin.ConnectorFunc(datadogConnector))
	if err != nil {
		log.Fatal(err)
	}

	log.Fatal(datadogPlugin.StartPlugin())
}

/*** Helpers ***/
func toStats(mm producers.MetricsMessage, ddURL string) error {
	c, err := statsd.New(ddURL)
	if err != nil {
		return err
	}

	c.Namespace = "dcos-metrics"

	tags, err := buildTags(mm.Dimensions)
	if err != nil {
		return err
	}

	for _, dp := range mm.Datapoints {
		name := strings.Join([]string{mm.Name, dp.Name}, producers.MetricNamespaceSep)
		val, ok := dp.Value.(float64)
		if !ok {
			return errors.New("Datapoint can not be coerced to float64")
		}

		err = c.Gauge(name, val, tags, 1)
		if err != nil {
			return err
		}
	}

	return nil
}

// buildTags analyzes the MetricsMessage.Dimensions struct and returns a slice
// of strings of key/value pairs in the format "key:value"
func buildTags(msg producers.Dimensions) (tags []string, err error) {
	v := reflect.ValueOf(msg)
	if v.Kind() != reflect.Struct {
		return tags, fmt.Errorf("error: expected type struct, got %s", v.Kind().String())
	}

	for i := 0; i < v.NumField(); i++ {
		// The producers.Dimensions struct contains a nested
		// map[string]string for user-defined Labels
		if v.Field(i).Kind() == reflect.Map {
			for k, v := range v.Field(i).Interface().(map[string]string) {
				tags = append(tags, strings.Join([]string{k, v}, ":"))
			}
			continue
		}

		fieldInfo := v.Type().Field(i)

		fieldVal := fmt.Sprintf("%v", v.Field(i).Interface())
		if fieldVal == reflect.Zero(fieldInfo.Type).String() {
			// don't include keys without a value
			continue
		}

		fieldTag := strings.Split(fieldInfo.Tag.Get("json"), ",")[0] // remove "omitempty" if present
		tags = append(tags, strings.Join([]string{fieldTag, fieldVal}, ":"))
	}

	return tags, nil
}
