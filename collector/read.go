package main

import (
	"log"
	"net"
	"strconv"
	"time"
)

// A packet of raw data produced by the UDP read socket, and the address where that data came from.
type Packet struct {
	src_addr *net.UDPAddr
	data []byte
}

func newPacket(addr *net.UDPAddr, buf []byte, n int) Packet {
	pkt := Packet{src_addr: new(net.UDPAddr), data: make([]byte, n)}

	*pkt.src_addr = *addr
	pkt.src_addr.IP = make(net.IP, len(addr.IP))
	copy(pkt.src_addr.IP, addr.IP)

	copy(pkt.data, buf[:n])

	return pkt
}

// Read is a function which continuously retrieves data from the provided UDP connection and
// forwards it as Packets to the provided output stream.
// This function is intended to be run as a gofunc.
func read(conn *net.UDPConn, out chan<- Packet, counters <-chan CountersRequest) {
	byte_count := 0
	packet_count := 0
	empty_packet_count := 0
	read_error_count := 0
    buf := make([]byte, udp_packet_max_size)
    for {
		select {
		case request := <- counters:
			request.response <- CountersResponse{
				label: "read",
				counters: []Counter{
					Counter{name: "bytes", value: strconv.Itoa(byte_count)},
					Counter{name: "all_packets", value: strconv.Itoa(packet_count)},
					Counter{name: "empty_packets", value: strconv.Itoa(empty_packet_count)},
					Counter{name: "read_errors", value: strconv.Itoa(read_error_count)},
				},
			}
			if request.exit {
				return
			}
		default:
		}

		conn.SetReadDeadline(time.Now().Add(read_timeout))
		n, src_addr, err := conn.ReadFromUDP(buf)
		switch {
		case err != nil:
			// complain only if the error isn't a timeout
			if opErr, typeMatch := err.(*net.OpError); !typeMatch || !opErr.Timeout() {
				log.Printf("Error receiving data on UDP endpoint %s, resuming: %s",
					conn.LocalAddr(), err)
				read_error_count++
			}
		case n == 0:
			empty_packet_count++
			packet_count++
			log.Printf("Got 0-byte UDP message from %s, resuming", src_addr)
		default:
			byte_count += n
			packet_count++
			packet := newPacket(src_addr, buf, n)
			//log.Printf("Received %db from %s: [%s]", n, packet.addr, string(packet.data))
			out <- packet
		}
    }
}
