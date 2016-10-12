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

package collector

// HostCollectorConfig ...
type HostCollectorConfig struct {
	MesosRole      string
	MesosPort      int
	MesosEndpoints map[string]string
	// Maybe testing config too? Unsure how those flags currently work.
}

// HostMetrics ...
type HostMetrics struct {
	Memory  string
	CPU     string
	Storage string
}

// Host ...
type Host struct {
	IPAddress string
	Hostname  string
	Metrics   HostMetrics
	Config    HostCollectorConfig
}

// SetIP ...
func (h *Host) SetIP() error { return nil }

// SetHostname ...
func (h *Host) SetHostname() error { return nil }

// SetConfig ...
func (h *Host) SetConfig(role string, port int, endpoints map[string]string) error { return nil }

// GetHostMetrics ...
func (h *Host) GetHostMetrics() error { return nil }
