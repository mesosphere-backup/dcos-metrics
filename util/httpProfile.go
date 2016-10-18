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

package util

import (
	"net"
	"net/http"
	_ "net/http/pprof" // register all pprof HTTP handlers

	log "github.com/Sirupsen/logrus"
)

// RunHTTPProfAccess runs an HTTP listener on a random ephemeral port which allows pprof access.
// This function should be run as a gofunc.
func RunHTTPProfAccess() {
	// listen on an ephemeral port, then print the port
	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		log.Printf("Unable to open profile access listener: %s\n", err)
	} else {
		log.Printf("Enabling profile access at http://%s/debug/pprof\n", listener.Addr())
		log.Println(http.Serve(listener, nil)) // blocks
	}
}
