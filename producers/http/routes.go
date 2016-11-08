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
	"fmt"
	"net/http"
	"strings"
)

// Route defines a single new route for gorilla/mux. This includes an arbitrary
// name, the HTTP method(s) allowed, the path for the endpoint, and the
// handler function in producers/http/handlers.go.
//
// When one or more Route structures are defined in a slice, they can be used
// to programatically create a gorilla/mux router, like so:
//
// 		for _, route := range routes {
//  		router.NewRoute() ...
//
// Although arbitrary, up to this point our naming convention for Name has been:
//   * convert slashes to underscores
//   * remove any params ('id', etc) from the Name
//
type Route struct {
	Name        string
	Method      string
	Path        string
	HandlerFunc func(*producerImpl) http.HandlerFunc
}

var (
	version = 0
	root    = fmt.Sprintf("/api/v%d", version)
)

var routes = []Route{

	// Root endpoint, e.g. /api/v0...
	Route{
		Name:        "root",
		Method:      "GET",
		Path:        root,
		HandlerFunc: fooHandler,
	},

	// Agent Endpoints, e.g. /api/v0/agent...
	Route{
		Name:        "agent",
		Method:      "GET",
		Path:        strings.Join([]string{root, "agent"}, "/"),
		HandlerFunc: agentHandler,
	},
	Route{
		Name:        "agent_cpu",
		Method:      "GET",
		Path:        strings.Join([]string{root, "agent", "cpu"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "agent_memory",
		Method:      "GET",
		Path:        strings.Join([]string{root, "agent", "memory"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "agent_disks",
		Method:      "GET",
		Path:        strings.Join([]string{root, "agent", "disks"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "agent_networks",
		Method:      "GET",
		Path:        strings.Join([]string{root, "agent", "networks"}, "/"),
		HandlerFunc: fooHandler,
	},

	// Containers and apps endpoints,e.g. /api/v0/containers...
	Route{
		Name:        "containers",
		Method:      "GET",
		Path:        strings.Join([]string{root, "containers"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "containers",
		Method:      "GET",
		Path:        strings.Join([]string{root, "containers", "{id}"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "containers_cpu",
		Method:      "GET",
		Path:        strings.Join([]string{root, "containers", "{id}", "cpu"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "containers_memory",
		Method:      "GET",
		Path:        strings.Join([]string{root, "containers", "{id}", "memory"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "containers_filesystems",
		Method:      "GET",
		Path:        strings.Join([]string{root, "containers", "{id}", "filesystems"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "containers_network",
		Method:      "GET",
		Path:        strings.Join([]string{root, "containers", "{id}", "network"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "containers_app",
		Method:      "GET",
		Path:        strings.Join([]string{root, "containers", "{id}", "app"}, "/"),
		HandlerFunc: fooHandler,
	},
	Route{
		Name:        "containers_app_metric",
		Method:      "GET",
		Path:        strings.Join([]string{root, "containers", "{id}", "app", "{metric-id}"}, "/"),
		HandlerFunc: fooHandler,
	},
}
