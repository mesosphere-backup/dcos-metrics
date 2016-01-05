package main

import (
	//"bytes"
	"flag"
	"log"
    "net"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"
)

var statsd_listen_host =
	flag.String("statsd_listen_host", "", "host to listen on for incoming statsd data")
var statsd_listen_port =
	flag.Int("statsd_listen_port", 8125, "port to listen on for incoming statsd data")
var read_buffer_size =
	flag.Int("read_buffer_size", 100, "size of the buffer between reads and output channels")

const udp_packet_max_size = 65536 // UDP size limit in IPv4 (may be larger in IPv6)
const read_timeout = 100 * time.Millisecond
const framework_refresh_period = time.Minute
const missing_framework_prune_period = 24 * time.Hour

func resolve() *net.UDPAddr {
	listen_endpoint := *statsd_listen_host + ":" + strconv.Itoa(*statsd_listen_port)
	listen_addr, err := net.ResolveUDPAddr("udp", listen_endpoint)
	if err != nil {
		log.Fatalf("Unable to resolve UDP listen endpoint %s: %s", listen_endpoint, err)
	}
	return listen_addr
}

func main() {
	flag.Parse()

	log.Printf("PID: %d", os.Getpid())

	listen_addr := resolve()
    conn, err := net.ListenUDP("udp", listen_addr)
	if err != nil {
		log.Fatalf("Unable to listen on UDP endpoint %s: %s", listen_addr, err)
	}
	log.Printf("Listening for statsd at UDP endpoint %s", conn.LocalAddr())
    defer conn.Close()

	packets := make(chan Packet, *read_buffer_size)
	configs := make(chan Configuration)
	framework_state_request := make(chan FrameworkStateRequest) // FIXME have web frontend hit this for framework listing
	framework_state_ticker := time.NewTicker(framework_refresh_period)
	defer framework_state_ticker.Stop()
	framework_list := make(chan FrameworkListRequest)

	counters := []chan CountersRequest{
		make(chan CountersRequest),
		make(chan CountersRequest),
		make(chan CountersRequest),
		make(chan CountersRequest),
	}
	go configure(configs, counters[0])
	go framework_state(framework_state_request, framework_state_ticker.C, framework_list, counters[1])
	go read(conn, packets, counters[2])
	go route(packets, configs, framework_list, counters[3])

	signals := make(chan os.Signal)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM, syscall.SIGUSR1, syscall.SIGUSR2)
	for {
		signal := <- signals
		log.Printf("Got signal: %s",signal)

		switch signal {
		case syscall.SIGINT:
			fallthrough
		case syscall.SIGTERM:
			grab_counters(counters, true)
			log.Printf("Shutting down")
			return

		case syscall.SIGUSR1:
			fallthrough
		case syscall.SIGUSR2:
			grab_counters(counters, false)
		}
	}
}
