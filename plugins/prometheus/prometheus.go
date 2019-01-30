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
	"bytes"
	"fmt"
	"net"
	"net/http"
	"regexp"
	"strings"
	"sync"
	"time"

	log "github.com/sirupsen/logrus"
	"github.com/urfave/cli"

	plugin "github.com/dcos/dcos-metrics/plugins"
	"github.com/dcos/dcos-metrics/producers"
	prodHelpers "github.com/dcos/dcos-metrics/util/producers"
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
	registerOnce  sync.Once
	listener      net.Listener
	latestMetrics metricsSnapshot
	illegalChars  = regexp.MustCompile("\\W")
)

type metricsSnapshot struct {
	sync.Mutex
	metrics []producers.MetricsMessage
}

func main() {
	log.Info("Starting Prometheus DC/OS metrics plugin")

	promPlugin, err := plugin.New(
		plugin.Name("prometheus"),
		plugin.ExtraFlags(pluginFlags),
		plugin.ConnectorFunc(promConnector))

	if err != nil {
		log.Fatal(err)
	}

	promPlugin.BeforeFunc = startPromServer
	promPlugin.AfterFunc = stopPromServer

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
	return listener.Close()
}

func promConnector(metrics []producers.MetricsMessage, c *cli.Context) error {
	latestMetrics.Lock()
	latestMetrics.metrics = metrics
	latestMetrics.Unlock()
	return nil
}

func serveMetrics(w http.ResponseWriter, r *http.Request) {
	// operate on a copy of metrics to avoid TOCTOU issues
	metrics := []producers.MetricsMessage{}
	latestMetrics.Lock()
	metrics = latestMetrics.metrics
	latestMetrics.Unlock()

	if len(metrics) == 0 {
		http.Error(w, "", http.StatusNoContent)
		return
	}
	for _, m := range metrics {
		fmt.Fprintf(w, messageToPromText(m))
	}
}

// messageToPromText converts a single metrics message to prometheus-formatted
// newline-separate strings
func messageToPromText(message producers.MetricsMessage) string {
	var buffer bytes.Buffer

	for _, d := range message.Datapoints {
		name := sanitizeName(d.Name)
		labels := getLabelsForDatapoint(message.Dimensions, d.Tags)
		t, err := time.Parse(time.RFC3339, d.Timestamp)
		if err != nil {
			log.Warnf("Encountered bad timestamp, %q: %s", d.Timestamp, err)
			continue
		}
		timestampMs := int(t.UnixNano() / 1000000)
		buffer.WriteString(fmt.Sprintf("%s%s %v %d\n", name, labels, d.Value, timestampMs))
	}

	return buffer.String()
}

// getLabelsForDatapoint returns prometheus-formatted labels from a
// datapoint's dimensions and tags
func getLabelsForDatapoint(dimensions producers.Dimensions, tags map[string]string) string {
	allDimensions := map[string]string{}
	if dimensions.Labels != nil {
		allDimensions = dimensions.Labels
	}

	allDimensions["mesos_id"] = dimensions.MesosID
	allDimensions["cluster_id"] = dimensions.ClusterID
	allDimensions["container_id"] = dimensions.ContainerID
	allDimensions["framework_name"] = dimensions.FrameworkName
	allDimensions["framework_id"] = dimensions.FrameworkID
	allDimensions["framework_role"] = dimensions.FrameworkRole
	allDimensions["framework_principal"] = dimensions.FrameworkPrincipal
	allDimensions["task_name"] = dimensions.TaskName
	allDimensions["task_id"] = dimensions.TaskID
	allDimensions["hostname"] = dimensions.Hostname

	labels := []string{}
	for k, v := range allDimensions {
		if len(v) > 0 {
			labels = append(labels, fmt.Sprintf("%s=%q", k, v))
		}
	}
	// Sorting tags ensures consistent order
	for _, pair := range prodHelpers.SortTags(tags) {
		k, v := pair[0], pair[1]
		labels = append(labels, fmt.Sprintf("%s=%q", sanitizeName(k), v))
	}

	if len(labels) > 0 {
		return "{" + strings.Join(labels, ",") + "}"
	}
	return ""
}

// sanitizeName returns a metric or label name which is safe for use
// in prometheus output
func sanitizeName(name string) string {
	return strings.ToLower(illegalChars.ReplaceAllString(name, "_"))
}
