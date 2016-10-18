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
)

// Response represents the structure of the JSON document presented by the
// HTTP producer for a query about metrics.
type Response struct {
	Name       string      `json:"name"`
	Datapoints []Datapoint `json:"datapoints"`
}

// Datapoint represents a single metric's timestamp, value, and unit in a response.
// A single datapoint is typically contained in an array, such as []Datapoint{}.
type Datapoint struct {
	Name      string `json:"name"`
	Value     string `json:"value"`
	Unit      string `json:"unit"`
	Timestamp string `json:"timestamp"` // time.RFC3339Nano, e.g. "2016-01-01T01:01:01.10000000Z"
}

// Dimensions represents various metadata about the metric, including (but not limited to)
// the cluster ID, the agent ID, the executor ID, and so on.
type Dimensions struct {
	ClusterID          string `json:"cluster_id"`
	AgentID            string `json:"agent_id"`
	FrameworkName      string `json:"framework_name"`
	FrameworkID        string `json:"framework_id"`
	FrameworkRole      string `json:"framework_role"`
	FrameworkPrincipal string `json:"framework_principal"`
	ExecutorID         string `json:"executor_id"`
	ContainerID        string `json:"container_id"`
}

func fooHandler(w http.ResponseWriter, r *http.Request) {
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
