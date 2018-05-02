// Copyright 2018 Mesosphere, Inc.
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

package statsd

import (
	"context"
	"fmt"
	"net"
	"testing"

	. "github.com/smartystreets/goconvey/convey"
)

func getConn(address string) *net.UDPConn {
	// send the current time (gauge) and the count of packets sent (counter)
	dest, _ := net.ResolveUDPAddr("udp", address)
	conn, _ := net.DialUDP("udp", nil, dest)
	return conn
}

func TestNew(t *testing.T) {
	Convey("Creating a new statsd collector", t, func() {
		ctx, cancelFunc := context.WithCancel(context.Background())
		defer cancelFunc()
		c := New(0, ctx)

		Convey("Should return the port on which the server is running", func() {
			mp := c.ServerPort
			// We passed in 0 - the OS should have picked a nonzero port
			So(mp, ShouldBeGreaterThan, 0)
		})
		Convey("Should intially yield an empty metrics channel", func() {
			mc := c.MetricsChan
			So(len(mc), ShouldEqual, 0)
		})
		Convey("Should pass statsd metrics into the collector's metrics channel", func() {
			conn := getConn(fmt.Sprintf("127.0.0.1:%d", c.ServerPort))
			defer conn.Close()
			stat := fmt.Sprintf("foo.bar:1234|g|#test_tag_key:test_tag_value")
			conn.Write([]byte(stat))
			select {
			case m := <-c.MetricsChan:
				So(len(m.Datapoints), ShouldEqual, 1)
				So(m.Datapoints[0].Name, ShouldEqual, "foo.bar")
			}
		})
	})
}
