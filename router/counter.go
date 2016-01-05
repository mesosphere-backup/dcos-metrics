package main

import (
	"log"
)

type Counter struct {
	name string
	value string
}
type CountersResponse struct {
	label string
	counters []Counter
}
type CountersRequest struct {
	response chan CountersResponse
	exit bool
}

// Grab_counters retrieves and prints counter values from a set of worker functions, optionally
// telling those functions to exit after they've produced the counter values.
func grab_counters(chans []chan CountersRequest, exit bool) {
	request := CountersRequest{
		response: make(chan CountersResponse),
		exit: exit,
	}
	log.Printf("Retrieving counters from %d workers...", len(chans))
	for _, counter_chan := range chans {
		counter_chan <- request
		response := <- request.response
		log.Printf("  %s:", response.label)
		for _, counter := range response.counters {
			log.Printf("    %s = %s", counter.name, counter.value)
		}
	}
}
