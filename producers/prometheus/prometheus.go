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

package prometheus

import (
	"errors"
	"fmt"
	"math"
	"net/http"
	"regexp"
	"strings"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/store"
	"github.com/dcos/dcos-metrics/producers"
	prodHelpers "github.com/dcos/dcos-metrics/util/producers"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

var (
	illegalChars = regexp.MustCompile("\\W")
	// This is the same regex that Prometheus uses to test validity
	legalLabel = regexp.MustCompile("^[a-zA-Z_]([a-zA-Z0-9_])*$")
	promLog    = log.WithFields(log.Fields{"producer": "prometheus"})
)

// Config is a configuration for the Prom producer's behaviour
type Config struct {
	Port        int `yaml:"port"`
	CacheExpiry time.Duration
}

// promProducer implements both producers.MetricsProducer and
// prometheus.Producer
type promProducer struct {
	config             Config
	store              store.Store
	metricsChan        chan producers.MetricsMessage
	janitorRunInterval time.Duration
}

// datapointLabels associates a datapoint with its labels.
type datapointLabels struct {
	datapoint *producers.Datapoint
	labels    map[string]string
}

// New returns a prometheus producer and a channel for passing in metrics
func New(cfg Config) (producers.MetricsProducer, chan producers.MetricsMessage) {
	p := promProducer{
		config:             cfg,
		store:              store.New(),
		metricsChan:        make(chan producers.MetricsMessage),
		janitorRunInterval: 60 * time.Second,
	}
	return &p, p.metricsChan
}

func (p *promProducer) Run() error {
	promLog.Info("Starting Prom producer garbage collection service")
	go p.janitor()

	// The below is borrowed wholesale from the http producer.
	// TODO (philipnrmn): rewrite this section to avoid use of the store
	go func() {
		promLog.Debug("Prom producer listening for incoming messages on metricsChan")
		for {
			// read messages off the channel,
			// and give them a unique name in the store
			message := <-p.metricsChan

			var name string
			switch message.Name {
			case producers.NodeMetricPrefix:
				name = joinMetricName(message.Name, message.Dimensions.MesosID)

			case producers.ContainerMetricPrefix:
				name = joinMetricName(message.Name, message.Dimensions.ContainerID)

			case producers.AppMetricPrefix:
				name = joinMetricName(message.Name, message.Dimensions.ContainerID)
			}

			for _, d := range message.Datapoints {
				p.writeObjectToStore(d, message, name)
			}
		}
	}()

	registry := prometheus.NewRegistry()
	registry.MustRegister(p)

	mux := http.NewServeMux()
	mux.Handle("/metrics", promhttp.HandlerFor(
		registry, promhttp.HandlerOpts{ErrorHandling: promhttp.ContinueOnError}))

	addr := fmt.Sprintf(":%d", p.config.Port)
	server := &http.Server{
		Addr:    addr,
		Handler: mux,
	}

	promLog.Infof("Serving Prometheus metrics on %s", addr)
	return server.ListenAndServe()
}

// Describe passes all the available stat descriptions to Prometheus; we
// send a single 'dummy' stat description, and then create our actual
// descriptions on the fly in Collect() below.
// This is necessary because Describe is expected to yield all possible
// metrics to the channel, and must therefore send at least one value.
// For a full description see:
// https://godoc.org/github.com/prometheus/client_golang/prometheus#Collector
func (p *promProducer) Describe(ch chan<- *prometheus.Desc) {
	prometheus.NewGauge(prometheus.GaugeOpts{Name: "Dummy", Help: "Dummy"}).Describe(ch)
}

// Collect iterates over all the metrics available in the store, converting
// them to prometheus.Metric and passing them into the prometheus producer
// channel, where they will be served to consumers.
func (p *promProducer) Collect(ch chan<- prometheus.Metric) {
	for name, datapointsLabels := range datapointLabelsByName(p.store) {
		variableLabels, constLabels := getDescLabels(datapointsLabels)
		desc := prometheus.NewDesc(sanitize(name), "DC/OS Metrics Datapoint", sanitizeSlice(variableLabels), sanitizeKeys(constLabels))

		// Publish a metric for each datapoint.
		for _, dpLabels := range datapointsLabels {
			// Collect this datapoint's variable label values.
			var variableLabelVals []string
			for _, labelName := range variableLabels {
				if val, ok := dpLabels.labels[labelName]; ok {
					variableLabelVals = append(variableLabelVals, val)
				} else {
					variableLabelVals = append(variableLabelVals, "")
				}
			}

			val, err := coerceToFloat(dpLabels.datapoint.Value)
			if err != nil {
				promLog.Debugf("Bad datapoint value %q: (%s) %s", dpLabels.datapoint.Value, dpLabels.datapoint.Name, err)
				continue
			}

			metric, err := prometheus.NewConstMetric(desc, prometheus.GaugeValue, val, variableLabelVals...)
			if err != nil {
				promLog.Warnf("Could not create Prometheus metric %s: %s", name, err)
				continue
			}

			promLog.Debugf("Emitting datapoint %s", name)
			ch <- metric
		}
	}
}

// - helpers -

// writeObjectToStore writes a prefixed datapoint into the store.
func (p *promProducer) writeObjectToStore(d producers.Datapoint, m producers.MetricsMessage, prefix string) {
	newMessage := producers.MetricsMessage{
		Name:       m.Name,
		Datapoints: []producers.Datapoint{d},
		Dimensions: m.Dimensions,
		Timestamp:  m.Timestamp,
	}
	// e.g. dcos.metrics.app.[ContainerId].kafka.server.ReplicaFetcherManager.MaxLag
	qualifiedName := joinMetricName(prefix, d.Name)
	for _, pair := range prodHelpers.SortTags(d.Tags) {
		k, v := pair[0], pair[1]
		// e.g. dcos.metrics.node.[MesosId].network.out.errors.#interface:eth0
		serializedTag := fmt.Sprintf("#%s:%s", k, v)
		qualifiedName = joinMetricName(qualifiedName, serializedTag)
	}
	p.store.Set(qualifiedName, newMessage)
}

// joinMetricName concatenates its arguments using the metric name separator
func joinMetricName(segments ...string) string {
	return strings.Join(segments, producers.MetricNamespaceSep)
}

// janitor analyzes the objects in the store and removes stale objects. An
// object is considered stale when the top-level timestamp of its MetricsMessage
// has exceeded the CacheExpiry, which is calculated as a multiple of the
// collector's polling period. This function should be run in its own goroutine.
func (p *promProducer) janitor() {
	ticker := time.NewTicker(p.janitorRunInterval)
	for {
		select {
		case _ = <-ticker.C:
			for k, v := range p.store.Objects() {
				o := v.(producers.MetricsMessage)

				age := time.Now().Sub(time.Unix(o.Timestamp, 0))
				if age > p.config.CacheExpiry {
					promLog.Debugf("Removing stale object %s; last updated %d seconds ago", k, age/time.Second)
					p.store.Delete(k)
				}
			}
		}
	}
}

// sanitize returns a string which is safe to use as a metric or label name in Prometheus output.
// Unsafe characters are replaced by `_`. If name begins with a numeral, it is prepended with `_`.
func sanitize(name string) string {
	output := strings.ToLower(illegalChars.ReplaceAllString(name, "_"))

	if legalLabel.MatchString(output) {
		return output
	}
	// Prefix name with _ if it begins with a number
	return "_" + output
}

// sanitizeSlice returns a []string whose elements have been sanitized.
func sanitizeSlice(names []string) []string {
	sanitized := []string{}
	for _, n := range names {
		sanitized = append(sanitized, sanitize(n))
	}
	return sanitized
}

// sanitizeKeys returns a map[string]string whose keys have been sanitized.
func sanitizeKeys(m map[string]string) map[string]string {
	sanitized := map[string]string{}
	for k, v := range m {
		sanitized[sanitize(k)] = v
	}
	return sanitized
}

// coerceToFloat attempts to convert an interface to float64. It should succeed
// with any numeric input.
func coerceToFloat(unk interface{}) (float64, error) {
	switch i := unk.(type) {
	case float64:
		if math.IsNaN(i) {
			return i, errors.New("value was NaN")
		}
		return i, nil
	case float32:
		return float64(i), nil
	case int:
		return float64(i), nil
	case int32:
		return float64(i), nil
	case int64:
		return float64(i), nil
	case uint:
		return float64(i), nil
	case uint32:
		return float64(i), nil
	case uint64:
		return float64(i), nil
	default:
		return math.NaN(), fmt.Errorf("value %q could not be coerced to float64", i)
	}
}

// dimsToMap converts a Dimensions object to a flat map of strings to strings
func dimsToMap(dims producers.Dimensions) map[string]string {
	results := map[string]string{
		"mesos_id":            dims.MesosID,
		"cluster_id":          dims.ClusterID,
		"container_id":        dims.ContainerID,
		"executor_id":         dims.ExecutorID,
		"framework_name":      dims.FrameworkName,
		"framework_id":        dims.FrameworkID,
		"framework_role":      dims.FrameworkRole,
		"framework_principal": dims.FrameworkPrincipal,
		"task_name":           dims.TaskName,
		"task_id":             dims.TaskID,
		"hostname":            dims.Hostname,
	}
	for k, v := range dims.Labels {
		results[k] = v
	}
	return results
}

// datapointLabelsByName returns a mapping of datapoint names to a slice of structs pairing producers.Datapoints by that
// name with their corresponding labels. Labels are derived from MetricsMessage dimensions and Datapoint tags.
func datapointLabelsByName(s store.Store) map[string][]datapointLabels {
	rval := map[string][]datapointLabels{}

	for _, obj := range s.Objects() {
		m, ok := obj.(producers.MetricsMessage)
		if !ok {
			promLog.Warnf("Unsupported message type %T", obj)
			continue
		}

		// For each datapoint, collect its labels and add an entry to rval.
		dims := dimsToMap(m.Dimensions)
		for _, d := range m.Datapoints {
			labels := map[string]string{}
			for k, v := range dims {
				labels[k] = v
			}
			for k, v := range d.Tags {
				labels[k] = v
			}
			rval[d.Name] = append(rval[d.Name], datapointLabels{&d, labels})
		}
	}

	return rval
}

// labelValueIsConstant returns true if the value of labelName is constant among datapointsLabels.
func labelValueIsConstant(labelName string, datapointsLabels []datapointLabels) bool {
	// Initialize lastVal with the first datapoint's label value.
	lastVal, ok := datapointsLabels[0].labels[labelName]
	if !ok {
		// The label is missing from this datapoint, so its value must not be constant.
		return false
	}

	// Compare each datapoint's label value to the label value of the datapoint before it. If we find unequal values,
	// the label value is not constant.
	for _, dpLabels := range datapointsLabels[1:] {
		val, ok := dpLabels.labels[labelName]
		if !ok || val != lastVal {
			// The label is missing from this datapoint, or its value doesn't match the previous datapoint's value. Its
			// value is not constant.
			return false
		}
		// Update lastVal with the current label value before starting the next iteration.
		lastVal = val
	}
	// We didn't find unequal label values, so this label's value is constant.
	return true
}

// getDescLabels returns the variable lables and constant labels for the prometheus.Desc shared among datapointsLabels.
func getDescLabels(datapointsLabels []datapointLabels) (variableLabels []string, constLabels map[string]string) {
	// Collect all label names used among datapointsLabels.
	labelNames := map[string]bool{}
	for _, kv := range datapointsLabels {
		for k := range kv.labels {
			labelNames[k] = true
		}
	}

	// Collect variableLabels and constLabels.
	// variableLabels contains the names of labels whose values vary among these datapoints. constLabels contains the
	// names and values of labels which are constant among these datapoints.
	constLabels = map[string]string{}
	for labelName := range labelNames {
		if labelValueIsConstant(labelName, datapointsLabels) {
			// The label's value is constant, so we can grab it from any element.
			constLabels[labelName] = datapointsLabels[0].labels[labelName]
		} else {
			variableLabels = append(variableLabels, labelName)
		}
	}

	return variableLabels, constLabels
}
