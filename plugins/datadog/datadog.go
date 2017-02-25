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
	"os"
	"reflect"
	"strings"
	"time"

	"github.com/DataDog/datadog-go/statsd"
	log "github.com/Sirupsen/logrus"
	plugin "github.com/dcos/dcos-metrics/plugins"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/urfave/cli"
)

func main() {
	log.Info("Starting datadog DC/OS metrics plugin")

	ddPluginFlags := []cli.Flag{
		cli.StringFlag{
			Name:  "datadog-host",
			Value: "localhost",
			Usage: "Datadog URL to query",
		},
		cli.StringFlag{
			Name:  "datadog-port",
			Value: "8125",
			Usage: "Datadog port",
		},
	}

	datadogPlugin, err := plugin.New(ddPluginFlags)
	if err != nil {
		log.Fatal(err)
	}

	datadogPlugin.App.Action = func(c *cli.Context) error {
		for {
			messages, err := datadogPlugin.Metrics()
			if err != nil {
				log.Fatal(err)
			}

			if len(messages) == 0 {
				log.Error("No messages received from metrics service")
			} else {
				for _, msg := range messages {
					// Send the data on to datadog
					log.Info("Sending metrics to datadog...")
					datadogAgent := net.JoinHostPort(c.String("datadog-host"), c.String("datadog-port"))
					if err := toStats(msg, datadogAgent); err != nil {
						log.Errorf("Errors encountered sending metrics to Datadog: %s", err.Error())
						continue
					}
				}
			}

			log.Infof("Polling complete, sleeping for %d seconds", datadogPlugin.PollingInterval)
			time.Sleep(time.Duration(datadogPlugin.PollingInterval) * time.Second)
		}
		return nil
	}

	datadogPlugin.App.Run(os.Args)
}

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
				tags = append(tags, strings.Join([]string{k, v}, "."))
			}
			continue
		}

		fieldInfo := v.Type().Field(i)
		fieldTag := strings.Split(fieldInfo.Tag.Get("json"), ",")[0] // remove "omitempty" if present
		fieldVal := fmt.Sprintf("%v", v.Field(i).Interface())

		if fieldVal == reflect.Zero(fieldInfo.Type).String() {
			// don't include keys without a value
			continue
		}

		tag := strings.Join([]string{fieldTag, fieldVal}, ".")
		tags = append(tags, tag)
	}

	return tags, nil
}
