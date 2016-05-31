package main

import (
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"runtime"
	"strconv"
	"time"
)

const (
	envvarHost = "STATSD_UDP_HOST"
	envvarPort = "STATSD_UDP_PORT"
)

var debug_enabled bool

type ByteCount struct {
	success int64
	failed  int64
}

func (b *ByteCount) add(b2 ByteCount) {
	b.success += b2.success
	b.failed += b2.failed
}

func debugf(format string, v ...interface{}) {
	if !debug_enabled {
		return
	}
	log.Printf(format, v...)
}

func getEnvAddress() string {
	host := os.Getenv(envvarHost)
	if host == "" {
		log.Printf("No UDP Host provided in environment: Need %s", envvarHost)
	}
	port_str := os.Getenv(envvarPort)
	if port_str == "" {
		log.Printf("No UDP Port provided in environment: Need %s", envvarPort)
	}
	if host == "" || port_str == "" {
		log.Fatalf("Environment: %s", os.Environ())
	}
	port, err := strconv.Atoi(port_str)
	if err != nil {
		log.Fatalf("Invalid UDP Port provided in environment (%s): %s=%s %s",
			os.Environ(), envvarPort, port_str, err)
	}
	return host + ":" + strconv.Itoa(port)
}

func getConn(address string) *net.UDPConn {
	// send the current time (gauge) and the count of packets sent (counter)
	dest, err := net.ResolveUDPAddr("udp", address)
	if err != nil {
		log.Fatalf("Unable to resolve provided statsd endpoint (%s): %s", address, err)
	}
	conn, err := net.DialUDP("udp", nil, dest)
	if err != nil {
		log.Fatalf("Unable to dial provided statsd endpoint (%s/%s): %s", address, dest, err)
	}
	return conn
}

func formatCounteri(key string, val int64) string {
	return formati(key, val, "c")
}
func formatGaugei(key string, val int64) string {
	return formati(key, val, "g")
}
func formatGaugef(key string, val float64) string {
	return formatf(key, val, "g")
}
func formatTimerMsi(key string, val int64) string {
	return formati(key, val, "ms")
}
func formati(key string, val int64, tipe string) string {
	return fmt.Sprintf("statsd_emitter.%s:%s|%s|#sample_tag:sample_val",
		key, strconv.FormatInt(val, 10), tipe)
}
func formatf(key string, val float64, tipe string) string {
	return fmt.Sprintf("statsd_emitter.%s:%s|%s|#sample_tag:sample_val",
		key, strconv.FormatFloat(val, 'f', -1, 64), tipe)
}

func sendCounteri(conn *net.UDPConn, key string, val int64) ByteCount {
	return send(conn, formatCounteri(key, val))
}
func sendGaugei(conn *net.UDPConn, key string, val int64) ByteCount {
	return send(conn, formatGaugei(key, val))
}
func sendGaugef(conn *net.UDPConn, key string, val float64) ByteCount {
	return send(conn, formatGaugef(key, val))
}
func sendTimerMsi(conn *net.UDPConn, key string, val int64) ByteCount {
	return send(conn, formatTimerMsi(key, val))
}
func send(conn *net.UDPConn, msg string) ByteCount {
	debugf("SEND (%d): %s", len(msg), msg)
	sent, err := conn.Write([]byte(msg))
	if err != nil {
		debugf("Failed to send %d bytes: %s", len(msg), err)
		return ByteCount{success: 0, failed: int64(len(msg))}
	}
	return ByteCount{success: int64(sent), failed: 0}
}

func main() {
	start := time.Now()

	flag.BoolVar(&debug_enabled, "debug", false, "Enables debug log messages")
	flag.Parse()

	log.SetPrefix("statsd-emitter ")

	addr := getEnvAddress()
	log.Printf("Sending to: %s", addr)
	conn := getConn(addr)
	defer conn.Close()

	var bytes ByteCount
	bytes.add(sendCounteri(conn, "proc.instance_counter", 1))

	var loop_count int64 = 0
	for {
		bytes_start := bytes
		debugf("-- %d", loop_count)

		now := time.Now()
		bytes.add(sendGaugei(conn, "time.unix", int64(now.Unix())))
		bytes.add(sendGaugei(conn, "time.unix_nano", int64(now.UnixNano())))
		bytes.add(sendGaugef(conn, "time.unix_float", float64(now.UnixNano())/1000000000))
		uptime_ms := int64((now.UnixNano() - start.UnixNano()) / 1000000 /* nano -> milli */)
		bytes.add(sendGaugei(conn, "time.uptime_gauge_ms", uptime_ms))
		bytes.add(sendTimerMsi(conn, "time.uptime_timer_ms", uptime_ms))

		bytes.add(sendGaugei(conn, "proc.pid", int64(os.Getpid())))

		var memstats runtime.MemStats
		runtime.ReadMemStats(&memstats) // Alloc, TotalAlloc, Sys, Lookups, Mallocs, Frees
		bytes.add(sendGaugei(conn, "mem.alloc", int64(memstats.Alloc)))
		bytes.add(sendGaugei(conn, "mem.total_alloc", int64(memstats.TotalAlloc)))
		bytes.add(sendGaugei(conn, "mem.sys", int64(memstats.Sys)))
		bytes.add(sendGaugei(conn, "mem.lookups", int64(memstats.Lookups)))
		bytes.add(sendGaugei(conn, "mem.mallocs", int64(memstats.Mallocs)))
		bytes.add(sendGaugei(conn, "mem.frees", int64(memstats.Frees)))

		bytes.add(sendCounteri(conn, "sent.loop_counter", 1))
		bytes.add(sendGaugei(conn, "sent.loop_gauge", loop_count))
		bytes.add(sendGaugei(conn, "sent.bytes_success", bytes.success))
		bytes.add(sendGaugei(conn, "sent.bytes_failed", bytes.failed))

		loop_count++

		success := bytes.success - bytes_start.success
		failed := bytes.failed - bytes_start.failed
		log.Printf("Bytes sent: %d success, %d failed, %d total",
			success, failed, success+failed)
		time.Sleep(time.Second * 1)
	}
}
