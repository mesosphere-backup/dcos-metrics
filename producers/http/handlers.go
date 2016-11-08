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

	"github.com/dcos/dcos-metrics/producers"
)

func agentHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var am []interface{}
		agentMetrics, err := p.store.GetByRegex(producers.AgentMetricPrefix + ".*")
		if err != nil {
			w.WriteHeader(http.StatusInternalServerError)
		}

		for _, v := range agentMetrics {
			am = append(am, v)
		}

		w.Header().Set("Content-Type", "application/json; charset=UTF-8")
		if err := json.NewEncoder(w).Encode(am[0]); err != nil {
			w.WriteHeader(http.StatusInternalServerError)
		} else {
			w.WriteHeader(http.StatusOK)
		}
	}
}

func containersHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var cm []interface{}
		containerMetrics, err := p.store.GetByRegex(producers.ContainerMetricPrefix + ".*")
		if err != nil {
			w.WriteHeader(http.StatusInternalServerError)
		}

		for _, v := range containerMetrics {
			cm = append(cm, v)
		}

		w.Header().Set("Content-Type", "application/json; charset=UTF-8")
		if err := json.NewEncoder(w).Encode(cm[0]); err != nil {
			w.WriteHeader(http.StatusInternalServerError)
		} else {
			w.WriteHeader(http.StatusOK)
		}
	}
}

func fooHandler(p *producerImpl) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		type fooData struct {
			Message string `json:"message"`
		}

		result := fooData{Message: "Hello, world!"}

		w.Header().Set("Content-Type", "application/json; charset=UTF-8")
		w.WriteHeader(http.StatusOK)
		if err := json.NewEncoder(w).Encode(result); err != nil {
			panic(err)
		}
	}
}
