package main

import (
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
	"time"

	log "github.com/Sirupsen/logrus"
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

type stat struct {
	Key   string
	Value interface{}
	Unit  string
	Tags  map[string]string
}

func uptime(value int64) string {
	return fmt.Sprintf("statsd_tester.time.uptime:%d|g|#test_tag_key:test_tag_value", value)
}

func sendUptime(conn *net.UDPConn, value int64) ByteCount {
	return send(conn, uptime(value))
}

func send(conn *net.UDPConn, msg string) ByteCount {
	log.Debugf("SEND (%d): %s", len(msg), msg)
	sent, err := conn.Write([]byte(msg))
	if err != nil {
		log.Debugf("Failed to send %d bytes: %s", len(msg), err)
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

	uptime_ms := int64((now.UnixNano() - start.UnixNano()) / 1000000 /* nano -> milli */)
	bytes.add(sendUptime(conn, uptime_ms))

	return bytes
}

func main() {
	start := time.Now()
	flag.Parse()

	addr := getEnvAddress()
	log.Printf("Sending stats to: %s", addr)
	conn := getConn(addr)
	defer conn.Close()

	byteCountChan := make(chan ByteCount)
	go printOutputRateLoop(byteCountChan)

	var loopCount int64 = 0
	for {
		// send some random stats, record bytes sent
		log.Debugf("-- %d", loopCount)
		loopCount++
		byteCountChan <- sendSomeStats(conn, start, loopCount)

		if !*floodFlag {
			// wait a bit before sending more stats
			time.Sleep(time.Second * 1)
		}
	}
}
