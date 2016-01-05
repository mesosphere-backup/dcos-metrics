package main

import (
	"strconv"
)

type Configuration struct {
	routes []RouteConfiguration
}
type RouteConfiguration struct {
	endpoint string
	frameworks []FrameworkConfiguration
	all_frameworks bool
}
type FrameworkConfiguration struct {
	id string
	//FIXME filters
}

// Configure is a function which periodically produces configuration to config_out.
// This function is intended to be run as a gofunc.
func configure(config_out chan<- Configuration, counters <-chan CountersRequest) {
	//TODO periodically send a config
	config_update_count := 0
	for {
		select {
		case request := <- counters:
			request.response <- CountersResponse{
				label: "configure",
				counters: []Counter{
					Counter{name: "config_updates", value: strconv.Itoa(config_update_count)},
				},
			}
			if request.exit {
				return
			}
		}
	}
}
