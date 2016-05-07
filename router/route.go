package main

import (
	"log"
	"net"
	"strconv"
)

// The representation of an output endpoint where some subset of incoming data should be forwarded.
type Route struct {
	config RouteConfiguration
	conn net.Conn
}
func newRoute(config RouteConfiguration) Route {
	conn, err := net.Dial("udp", config.endpoint)
	if err != nil {
		log.Fatalf("Unable to connect to UDP destination %s: %s", config.endpoint, err)
	}
	log.Printf("Opened output connection to UDP endpoint %s", conn.RemoteAddr())
	return Route{ config: config, conn: conn }
}

func (r *Route) send(rec Record) int {
	send_ok := false
	if r.config.all_frameworks {
		send_ok = true
	}
	for _, fmwk := range r.config.frameworks {
		if fmwk.id == rec.framework_id /* && FIXME enforce any fmwk filters here */ {
			send_ok = true
			break
		}
	}
	if !send_ok {
		return 0
	}
	sz, err := r.conn.Write(rec.data)//TODO aggregate outgoing records (when enabled in flags)
	if err != nil {
		log.Printf("Error sending data to UDP endpoint %s, resuming: %s", r.conn.RemoteAddr(), err)
	}
	return sz
}

// Route is a function which continuously forwards Packets from data_in to internally-managed
// outputs which are configurated according to the information produced by config and frameworks.
// This function is intended to be run as a gofunc.
func route(data_in <-chan Packet, configs <-chan Configuration, frameworks <-chan FrameworkListRequest, counters <-chan CountersRequest) {
	routes := make([]Route, 0)
	framework_ids := make(map[string]bool)
	config_update_count := 0
	packet_in_count := 0
	record_missing_framework_count := 0
	record_missing_routes_count := 0
	bytes_sent_count := 0
	record_skipped_count := 0
	record_sent_count := 0
    for {
		select {
		case request := <- counters:
			request.response <- CountersResponse{
				label: "route",
				counters: []Counter{
					Counter{name: "config_updates", value: strconv.Itoa(config_update_count)},
					Counter{name: "packets_in", value: strconv.Itoa(packet_in_count)},
					Counter{name: "records_missing_framework",
						value: strconv.Itoa(record_missing_framework_count)},
					Counter{name: "records_missing_routes",
						value: strconv.Itoa(record_missing_routes_count)},
					Counter{name: "bytes_sent", value: strconv.Itoa(bytes_sent_count)},
					Counter{name: "records_skipped", value: strconv.Itoa(record_skipped_count)},
					Counter{name: "records_sent", value: strconv.Itoa(record_sent_count)},
				},
			}
			if request.exit {
				return
			}
		case request := <- frameworks:
			request.response <- framework_ids
			framework_ids = make(map[string]bool)
		case packet := <- data_in:
			packet_in_count++
			for _, parsed_packet := range parse(packet) {
				if parsed_packet.framework_id == "" {
					record_missing_framework_count++
				} else {
					framework_ids[parsed_packet.framework_id] = true
				}
				if len(routes) == 0 {
					record_missing_routes_count++
				}
				for _, route := range routes {
					sz := route.send(parsed_packet)
					if sz != 0 {
						bytes_sent_count += sz
						record_sent_count++
					} else {
						record_skipped_count++
					}
				}
			}
		case config := <- configs:
			routes = make([]Route, len(config.routes))
			for i, route_config := range config.routes {
				routes[i] = newRoute(route_config)
			}
			config_update_count++
		}
    }
}
