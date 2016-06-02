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
	envvarHost   = "STATSD_UDP_HOST"
	envvarPort   = "STATSD_UDP_PORT"
	reportPeriod = 10 // seconds
)

var (
	debugFlag = flag.Bool("debug", false, "Enables debug log messages")
	floodFlag = flag.Bool("flood", false, "Floods the port with stats, for capacity testing")
)

type ByteCount struct {
	success int64
	failed  int64
}

func (b *ByteCount) add(b2 ByteCount) {
	b.success += b2.success
	b.failed += b2.failed
}
func (b *ByteCount) reset() {
	b.success = 0
	b.failed = 0
}

func debugf(format string, v ...interface{}) {
	if !*debugFlag {
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

func printOutputRateLoop(byteCountChan <-chan ByteCount) {
	ticker := time.NewTicker(time.Second * time.Duration(reportPeriod))
	var bc ByteCount
	for {
		select {
		case _ = <-ticker.C:
			total := bc.success + bc.failed
			log.Printf("Bytes sent: %d success, %d failed, %d total (%d B/s)",
				bc.success, bc.failed, total, total*(60./reportPeriod))
			bc.reset()
		case addme := <-byteCountChan:
			bc.add(addme)
		}
	}
}

func sendSomeStats(conn *net.UDPConn, start time.Time, loopCount int64) ByteCount {
	now := time.Now()

	var bytes ByteCount

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
	bytes.add(sendGaugei(conn, "sent.loop_gauge", loopCount))
	bytes.add(sendGaugei(conn, "sent.bytes_success", bytes.success))
	bytes.add(sendGaugei(conn, "sent.bytes_failed", bytes.failed))

	return bytes
}

func main() {
	start := time.Now()
	flag.Parse()

	log.SetPrefix("statsd-emitter ")

	addr := getEnvAddress()
	log.Printf("Sending stats to: %s", addr)
	conn := getConn(addr)
	defer conn.Close()

	byteCountChan := make(chan ByteCount)
	go printOutputRateLoop(byteCountChan)

	byteCountChan <- sendCounteri(conn, "proc.instance_counter", 1)

	var loopCount int64 = 0
	for {
		// send some random stats, record bytes sent
		debugf("-- %d", loopCount)
		loopCount++
		byteCountChan <- sendSomeStats(conn, start, loopCount)

		if !*floodFlag {
			// wait a bit before sending more stats
			time.Sleep(time.Second * 1)
		}
	}
}
