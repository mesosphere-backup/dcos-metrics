package collector

import (
	"log"
	"net"
	"net/http"
)

import _ "net/http/pprof"

// Runs an HTTP listener on a random ephemeral port which allows pprof access.
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
