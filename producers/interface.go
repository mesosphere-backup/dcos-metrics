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

package producers

import "strings"

var (
	// MetricNamespaceSep defines the separator for a metrics namespace
	MetricNamespaceSep = "."
	// ContainerMetricPrefix defines the prefix of container-level metrics
	ContainerMetricPrefix = strings.Join([]string{"dcos", "metrics", "container"}, MetricNamespaceSep)
	// NodeMetricPrefix defines the prefix of node-level metrics
	NodeMetricPrefix = strings.Join([]string{"dcos", "metrics", "node"}, MetricNamespaceSep)
	// AppMetricPrefix defines the prefix of app-level metrics
	AppMetricPrefix = strings.Join([]string{"dcos", "metrics", "app"}, MetricNamespaceSep)
)

// MetricsProducer defines an interface that the various producers must
// implement in order to receive, process, and present metrics to the caller or
// client. All producers must use the MetricsMessage structure to receive
// metrics, and they must implement their own struct for handling configuration.
//
// Further, although it isn't defined in this interface, it is recommended that
// producers must also create their own MetricsMessage channel to be
// used both in the implementation (e.g., &producerImpl{}) and to be returned
// to the caller. Doing so ensures that, in the future, multiple producers
// can be enabled at once (each producer has a dedicated chan).
type MetricsProducer interface {
	Run() error
}

// MetricsMessage defines the structure of the metrics being sent to the various
// producers. For every message sent from the collector to a producer, the
// following fields must be present.
type MetricsMessage struct {
	Name       string      `json:"-"`
	Datapoints []Datapoint `json:"datapoints"`
	Dimensions Dimensions  `json:"dimensions,omitempty"`
	Timestamp  int64       `json:"-"`
}

// Datapoint represents a single metric's timestamp, value, and unit in a response.
// A single datapoint is typically contained in an array, such as []Datapoint{}.
type Datapoint struct {
	Name      string            `json:"name"`
	Value     interface{}       `json:"value"`
	Unit      string            `json:"unit"`
	Timestamp string            `json:"timestamp"` // time.RFC3339, e.g. "2016-01-01T01:01:01Z"
	Tags      map[string]string `json:"tags,omitempty"`
}

// Dimensions are metadata about the metrics contained in a given MetricsMessage.
type Dimensions struct {
	MesosID            string            `json:"mesos_id"`
	ClusterID          string            `json:"cluster_id,omitempty"`
	ContainerID        string            `json:"container_id,omitempty"`
	ExecutorID         string            `json:"executor_id,omitempty"`
	FrameworkName      string            `json:"framework_name,omitempty"`
	FrameworkID        string            `json:"framework_id,omitempty"`
	FrameworkRole      string            `json:"framework_role,omitempty"`
	FrameworkPrincipal string            `json:"framework_principal,omitempty"`
	Hostname           string            `json:"hostname"`
	Labels             map[string]string `json:"labels,omitempty"` // map of arbitrary key/value pairs (aka "labels")
}
