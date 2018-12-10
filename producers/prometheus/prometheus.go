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
	type dataStruct struct {
		tags      map[string]string
		datapoint producers.Datapoint
	}

	// Each FQname has an element in the list, corresponding to a datapoint, and each datapoint has a map of tags
	tagsGroupedByName := map[string][]dataStruct{}

	for _, obj := range p.store.Objects() {
		message, ok := obj.(producers.MetricsMessage)
		if !ok {
			promLog.Warnf("Unsupported message type %T", obj)
			continue
		}
		dims := dimsToMap(message.Dimensions)
		for _, d := range message.Datapoints {
			tagsForThisDatapoint := map[string]string{}
			name := sanitizeName(d.Name)
			for k, v := range dims {
				goodKey := sanitizeName(k)
				tagsForThisDatapoint[goodKey] = v
			}
			for k, v := range d.Tags {
				goodKey := sanitizeName(k)

				tagsForThisDatapoint[goodKey] = v
			}
			sanitizedDatapointTags := map[string]string{}
			for k, v := range d.Tags {
				goodKey := sanitizeName(k)
				sanitizedDatapointTags[goodKey] = v
			}
			sanitizedDatapoint := producers.Datapoint{Name: d.Name, Value: d.Value, Unit: d.Unit, Timestamp: d.Timestamp, Tags: sanitizedDatapointTags}
			tagsGroupedByName[name] = append(tagsGroupedByName[name], dataStruct{tagsForThisDatapoint, sanitizedDatapoint})
		}

	}

	for fqName, datapointsTags := range tagsGroupedByName {
		// Get a list of all  keys common to this fqName

		allKeys := map[string]bool{}
		for _, kv := range datapointsTags {
			for k := range kv.tags {
				allKeys[k] = true
			}
		}

		// Figure out the constLabels. Do this by collecting the common KV pairs together.
		// At the same time we can figure out the variable labels by taking
		// the difference between the total set and the constLabels.
		commonToAll := map[string]bool{}

		for key := range allKeys {
			var tagVal []string

			for _, tagSet := range datapointsTags {
				if val, ok := tagSet.tags[key]; ok {
					tagVal = append(tagVal, val)
				}
			}
			// If the length differs, => 1+ datapoints doesn't have the tag, therefore not common to all
			if len(tagVal) != len(datapointsTags) {
				commonToAll[key] = false
			} else {
				check := true
				firstVal := tagVal[0]
				// Check the values, if any one of them doesn't match, then it's not common to all
				for _, val := range tagVal[1:] {
					if val != firstVal {
						check = false
						commonToAll[key] = false
					}
				}
				if check == true {
					// If check's value hasn't changed after traversing the values, then they must all be the same
					commonToAll[key] = true
				}
			}
		}

		commonKVSet := map[string]string{}
		var differingKVKeys []string
		for key, checkVal := range commonToAll {
			datapoint := datapointsTags[0]
			if checkVal == true {
				// Take the first datapoint since they're all identical
				commonKVSet[key] = datapoint.tags[key]
			} else {
				differingKVKeys = append(differingKVKeys, key)
			}
		}

		desc := prometheus.NewDesc(fqName, "DC/OS Metrics Datapoint", differingKVKeys, commonKVSet)

		for _, datapoint := range datapointsTags {

			var differingKVVals []string

			for _, tagName := range differingKVKeys {
				if val, ok := datapoint.tags[tagName]; ok {
					differingKVVals = append(differingKVVals, val)
				} else {
					differingKVVals = append(differingKVVals, "")
				}
			}

			dp := datapoint.datapoint
			val, err := coerceToFloat(datapoint.datapoint.Value)
			if err != nil {
				promLog.Debugf("Bad datapoint value %q: (%s) %s", dp.Value, dp.Name, err)
				continue
			}

			metric, err := prometheus.NewConstMetric(desc, prometheus.GaugeValue, val, differingKVVals...)

			if err != nil {
				promLog.Warnf("Could not create Prometheus metric %s: %s", fqName, err)
				continue
			}

			promLog.Debugf("Emitting datapoint %s", fqName)
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

// sanitizeName returns a metric or label name which is safe for use
// in prometheus output
func sanitizeName(name string) string {
	output := strings.ToLower(illegalChars.ReplaceAllString(name, "_"))

	if legalLabel.MatchString(output) {
		return output
	}
	// Prefix name with _ if it begins with a number
	return "_" + output
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
