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

package collectors

// MetricsCollector defines an interface that the various collectors must
// implement in order to collector, process, and present metrics to the caller
// or client. All collectors must use the MetricsMessage structure to receive
// metrics, and they must implement their own struct for handling configuration.
//
// Further, although it isn't defined in this interface, it is recommended that
// collectors also create their own MetricsMessage channel to be used both in
// the implementation (e.g., &collectorImpl{}) and to be returned to the caller.
// Doing so ensures that we don't end up implementing too much in main(),
// instead opting to push the complexity down into the individual
// collectors.
type MetricsCollector interface {
	Run() error
}

// NodeInfo represents information about the node, such as the IP address,
// hostname, Mesos ID, and the cluster ID that the node belongs to.
type NodeInfo struct {
	IPAddress string
	MesosID   string
	ClusterID string
	Hostname  string
}
