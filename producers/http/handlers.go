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

package http

import (
	"encoding/json"
	"fmt"
	"net/http"
	"regexp"
	"strings"

	"github.com/dcos/dcos-metrics/producers"
	"github.com/gorilla/mux"
)

// /api/v0/agent
func agentHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var am []interface{}
		agentMetrics, err := p.store.GetByRegex(producers.AgentMetricPrefix + ".*")
		handleErr(err, w)
		for _, v := range agentMetrics {
			am = append(am, v)
		}
		encode(am[0], w)
	}
}

// /api/v0/agent/{cpu, memory, ...}
func agentSingleMetricHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		vars := mux.Vars(r)
		metricFamily := vars["metricFamily"][0:3] // cpu, mem, dis, net
		agentMetrics, err := p.store.GetByRegex(producers.AgentMetricPrefix + ".*")
		handleErr(err, w)

		var am []producers.MetricsMessage
		for _, v := range agentMetrics {
			am = append(am, v.(producers.MetricsMessage))
		}

		datapoints := []producers.Datapoint{}
		for _, point := range am[0].Datapoints {
			p := strings.Split(point.Name, producers.MetricNamespaceSep)[5]
			if match, _ := regexp.MatchString(fmt.Sprintf("%s.*", metricFamily), p); match {
				datapoints = append(datapoints, point)
			}
		}
		am[0].Datapoints = datapoints
		encode(am[0], w)
	}
}

// /api/v0/containers
func containersHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var cm []producers.MetricsMessage
		containerMetrics, err := p.store.GetByRegex(producers.ContainerMetricPrefix + ".*")
		handleErr(err, w)

		for _, c := range containerMetrics {
			cm = append(cm, c.(producers.MetricsMessage))
		}
		encode(cm, w)
	}
}

// /api/v0/containers/{id}
func containerHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		vars := mux.Vars(r)
		key := strings.Join([]string{
			producers.ContainerMetricPrefix, vars["id"],
		}, producers.MetricNamespaceSep)
		containerMetrics, _ := p.store.Get(key)
		encode(containerMetrics, w)
	}
}

// /api/v0/containers/{id}/{cpu, memory, ...}
func containerSingleMetricHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		vars := mux.Vars(r)
		metricFamily := vars["metricFamily"][0:3] // cpu, mem, dis, net
		key := strings.Join([]string{
			producers.ContainerMetricPrefix, vars["id"],
		}, producers.MetricNamespaceSep)
		containerMetrics, _ := p.store.Get(key)

		cm := containerMetrics.(producers.MetricsMessage)
		datapoints := []producers.Datapoint{}
		for _, point := range cm.Datapoints {
			p := strings.Split(point.Name, producers.MetricNamespaceSep)[4]
			if match, _ := regexp.MatchString(fmt.Sprintf("%s.*", metricFamily), p); match {
				datapoints = append(datapoints, point)
			}
		}
		cm.Datapoints = datapoints
		encode(cm, w)
	}
}

func notYetImplementedHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		type fooData struct {
			Message string `json:"message"`
		}
		result := fooData{Message: "Not Yet Implemented"}
		encode(result, w)
	}
}

// -- helpers

func encode(v interface{}, w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/json; charset=UTF-8")
	if err := json.NewEncoder(w).Encode(v); err != nil {
		w.WriteHeader(http.StatusInternalServerError)
	} else {
		w.WriteHeader(http.StatusOK)
	}
}

func handleErr(err error, w http.ResponseWriter) {
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
	}
}
