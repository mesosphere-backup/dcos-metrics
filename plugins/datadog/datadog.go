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
	"context"
	"fmt"
	"net"
	"reflect"
	"strconv"
	"strings"
	"time"

	"github.com/DataDog/datadog-go/statsd"
	"google.golang.org/grpc"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/producers"
	pb "github.com/dcos/dcos-metrics/producers/plugin/plugin"
)

// Config defines the configuration of the DataDog metrics producer.
type config struct {
	DataDogHost   string
	DataDogPort   int
	PluginHost    string
	PluginPort    int
	RetryInterval time.Duration
}

func NewConfig() config {
	return config{
		DataDogHost: "localhost",
		DataDogPort: 8125,
		PluginHost:  "localhost",
		PluginPort:  9001,
	}
}

func main() {
	config := NewConfig()
	pluginAddress := fmt.Sprintf("%s:%d", config.PluginHost, config.PluginPort)
	// Set up a connection to the server.
	conn, err := grpc.Dial(pluginAddress, grpc.WithInsecure())
	if err != nil {
		log.Fatalf("did not connect: %v", err)
	}
	defer conn.Close()
	c := pb.NewMetricsClient(conn)

	metricsStream, err := c.AttachOutputStream(context.Background(), &pb.MetricsCollectorType{Type: "node"})
	if err != nil {
		log.Fatalf("Could not get metrics message from plugin producer: %v", err)
	}

	for {
		if r, err := metricsStream.Recv(); err != nil {
			log.Errorf("Error receiving metrics stream, %s", err.Error())
			time.Sleep(15 * time.Second)
		} else {
			log.Printf("Metric Received: %s", r.Name)
			for _, dp := range r.Datapoints {
				log.WithFields(log.Fields{
					"Metric Collector": r.Name,
				}).Infof("%s: %+v", dp.Name, dp.Value)
			}
		}
	}
}

// Eventually send metrics to datadog
func sendToDatadog(c config, message string) error {
	hostString := net.JoinHostPort(c.DataDogHost, strconv.Itoa(c.DataDogPort))
	log.Debug("DataDog producer listening for incoming messages on metricsChan")
	log.Debug("Configured to connect to the DataDog agent at %s")

	for {
		// TODO(roger): for the purposes of the dcos-metrics MVP, there is not
		// a way for the user to make dynamic configuration changes. Therefore,
		// the DataDog producer is always enabled and checks if the DataDog
		// agent is listening at localhost:8125. If it is, the connection will
		// be successful and metrics will be shipped to DataDog. If it isn't,
		// the connection will be retried based on cfg.RetryInterval.
		//
		// In the meantime... we will have metrics accumulating on metricsChan.
		// These metrics may never be sent to DataDog, because the agent may
		// or may not be running. Therefore, we read the message off the chan
		// *first*, thus discarding the data if the connection to the DataDog
		// agent was unsuccessful.
		//
		// In the future, this should be reworked to allow the user to enable or
		// disable the DataDog producer dynamically, thus allowing us to cache
		// metrics in memory for a given amount of time in the event the DataDog
		// agent is temporarily unavailable (being upgraded, for example). We'll
		// also be able to use a single DataDog client for multiple metrics,
		// instead of creating a new connection to the agent each time a metric
		// comes in on the channel.
		//message := <-p.metricsChan

		dog, err := statsd.New(hostString)
		if err != nil {
			log.Errorf("unable to connect to the datadog agent at %s, trying again in %s", hostString, c.RetryInterval.String())
			time.Sleep(c.RetryInterval)
			continue
		}
		defer dog.Close()

		// Need to update proto schema to support these sub message parameters.
		//		tags, err := buildTags(message.Dimensions)
		//		if err != nil {
		//			log.Errorf("error: unable to build tags: %s", err)
		//		}
		//
		//		// TODO(roger): everything is a gauge for now
		//		for _, dp := range message.Datapoints {
		//			name := strings.Join([]string{message.Name, dp.Name}, producers.MetricNamespaceSep)
		//
		//			// The DataDog client only accepts float64, so type doesn't matter here
		//			val, err := strconv.ParseFloat(fmt.Sprintf("%v", reflect.ValueOf(dp.Value).Interface()), 64)
		//			if err != nil {
		//				log.Errorf("error: unable to parse value as type float64: %s", err)
		//				continue
		//			}
		//
		//			err = dog.Gauge(name, val, tags, 1)
		//			if err != nil {
		//				log.Errorf("error: unable to publish metric '%s' to DataDog: %s", name, err)
		//				continue
		//			}
		//		}
	}
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
