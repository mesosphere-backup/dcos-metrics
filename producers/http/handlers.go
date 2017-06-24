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
	"net/http"
	"strings"
	"time"

	"github.com/dcos/dcos-metrics/producers"
	"github.com/gorilla/mux"
)

func nodeHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var am []interface{}
		nodeMetrics, err := p.store.GetByRegex(producers.NodeMetricPrefix + ".*")
		if err != nil {
			httpLog.Errorf("/v0/node - %s", err.Error())
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		for _, v := range nodeMetrics {
			am = append(am, v)
		}

		if len(am) != 0 {
			encode(am[0], w)
			return
		}

		httpLog.Error("/v0/node - no content in store.")
		http.Error(w, "No values found in store", http.StatusBadRequest)
	}
}

func containersHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		cm := []string{}
		containerMetrics, err := p.store.GetByRegex(producers.ContainerMetricPrefix + ".*")
		if err != nil {
			httpLog.Errorf("/v0/containers - %s", err.Error())
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}

		for _, c := range containerMetrics {
			if _, ok := c.(producers.MetricsMessage); !ok {
				httpLog.Errorf("/v0/containers - unsupported message type")
				http.Error(w, "Got unsupported message type.", http.StatusInternalServerError)
				return
			}
			cm = append(cm, c.(producers.MetricsMessage).Dimensions.ContainerID)
		}

		encode(cm, w)
	}
}

func containerHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		vars := mux.Vars(r)
		key := strings.Join([]string{
			producers.ContainerMetricPrefix, vars["id"],
		}, producers.MetricNamespaceSep)

		containerMetrics, ok := p.store.Get(key)
		if !ok {
			httpLog.Errorf("/v0/containers/{id} - not found in store: %s", key)
			http.Error(w, "Key not found in store", http.StatusNoContent)
			return
		}

		httpLog.Debugf("Encoding container metrics:\n%+v", containerMetrics)

		encode(containerMetrics, w)
	}
}

func containerAppHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		vars := mux.Vars(r)
		cid := vars["id"]
		key := strings.Join([]string{
			producers.AppMetricPrefix, cid,
		}, producers.MetricNamespaceSep)

		containerMetrics, err := p.store.GetByRegex(key + ".*")
		if err != nil {
			httpLog.Errorf("/v0/containers/{id}/app - %s", err.Error())
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		if len(containerMetrics) == 0 {
			httpLog.Errorf("/v0/containers/{id}/app - not found in store: %s", key)
			http.Error(w, "Key not found in store", http.StatusNoContent)
			return
		}

		var combinedMetrics producers.MetricsMessage
		for _, c := range containerMetrics {
			if _, ok := c.(producers.MetricsMessage); !ok {
				httpLog.Errorf("/v0/containers/{id}/app - unsupported message type")
				http.Error(w, "Got unsupported message type.", http.StatusInternalServerError)
				return
			}
			metric := c.(producers.MetricsMessage)
			combinedMetrics.Datapoints = append(combinedMetrics.Datapoints, metric.Datapoints...)
			combinedMetrics.Dimensions = metric.Dimensions
		}
		encode(combinedMetrics, w)
	}
}

func containerAppMetricHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		vars := mux.Vars(r)
		cid := vars["id"]
		mid := vars["metric-id"]
		key := strings.Join([]string{
			producers.AppMetricPrefix, cid,
		}, producers.MetricNamespaceSep)

		appMetrics, ok := p.store.Get(key)
		if !ok {
			httpLog.Errorf("/v0/containers/{id}/app/{metric-id} - not found in store: %s", key)
			http.Error(w, "Key not found in store", http.StatusNoContent)
			return
		}

		if _, ok := appMetrics.(producers.MetricsMessage); !ok {
			httpLog.Errorf("/v0/contianers - unsupported message type.")
			http.Error(w, "Got unsupported message type.", http.StatusInternalServerError)
			return
		}

		for _, dp := range appMetrics.(producers.MetricsMessage).Datapoints {
			if dp.Name == mid {
				m := producers.MetricsMessage{
					Datapoints: []producers.Datapoint{dp},
					Dimensions: appMetrics.(producers.MetricsMessage).Dimensions,
				}
				encode(m, w)
				return
			}
		}
		httpLog.Errorf("/v0/containers/{id}/app/{metric-id} - not found in store, CID: %s / Metric-ID: %s", key, mid)
		http.Error(w, "Metric not found in store", http.StatusNoContent)
	}
}

func pingHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		type ping struct {
			OK        bool   `json:"ok"`
			Timestamp string `json:"timestamp"`
		}

		encode(ping{OK: true, Timestamp: time.Now().UTC().Format(time.RFC3339)}, w)
	}
}

// -- helpers

func encode(v interface{}, w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/json; charset=UTF-8")
	if err := json.NewEncoder(w).Encode(v); err != nil {
		httpLog.Errorf("Failed to encode value to JSON: %v", v)
		http.Error(w, "Failed to encode value to JSON", http.StatusInternalServerError)
	}
}
