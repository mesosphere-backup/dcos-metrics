//+build unit

// Copyright 2016 Mesosphere, Inc.
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

package http

import (
	"fmt"
	"net"
	"os"
	"strconv"
	"testing"
	"time"

	"github.com/coreos/go-systemd/activation"
	"github.com/dcos/dcos-go/store"
	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

func TestNew(t *testing.T) {
	Convey("When creating a new instance of the HTTP producer", t, func() {
		Convey("Should return a new Collector instance", func() {
			pi, pc := New(Config{})
			So(pi, ShouldHaveSameTypeAs, &producerImpl{})
			So(pc, ShouldHaveSameTypeAs, make(chan producers.MetricsMessage))
		})
	})
}

func TestRun(t *testing.T) {
	Convey("When running the HTTP producer", t, func() {
		Convey("Should read messages off the metricsChan and write them to the store", func() {
			port, err := getEphemeralPort()
			if err != nil {
				panic(err)
			}

			p := producerImpl{
				config:             Config{Port: port},
				store:              store.New(),
				metricsChan:        make(chan producers.MetricsMessage),
				janitorRunInterval: 60 * time.Second,
			}
			go p.Run()
			time.Sleep(1 * time.Second)

			dps := []producers.Datapoint{
				producers.Datapoint{
					Name: "datapoint-one",
				},
			}
			p.metricsChan <- producers.MetricsMessage{
				Name:       "some-message",
				Datapoints: dps,
				Timestamp:  time.Now().UTC().Unix(),
			}
			time.Sleep(250 * time.Millisecond)

			So(p.store.Size(), ShouldEqual, 1)
		})

		Convey("Shoud not overwrite metrics delivered in sequence", func() {
			port, err := getEphemeralPort()
			if err != nil {
				panic(err)
			}

			p := producerImpl{
				config:             Config{Port: port},
				store:              store.New(),
				metricsChan:        make(chan producers.MetricsMessage),
				janitorRunInterval: 60 * time.Second,
			}
			go p.Run()
			time.Sleep(1 * time.Second)

			dp1 := []producers.Datapoint{
				producers.Datapoint{Name: "datapoint-one"},
			}
			dp2 := []producers.Datapoint{
				producers.Datapoint{Name: "datapoint-two"},
			}

			// Datapoint one is written
			p.metricsChan <- producers.MetricsMessage{
				Name:       "some-message",
				Datapoints: dp1,
				Timestamp:  time.Now().UTC().Unix(),
			}
			// Datapoint two is written
			p.metricsChan <- producers.MetricsMessage{
				Name:       "some-message",
				Datapoints: dp2,
				Timestamp:  time.Now().UTC().Unix(),
			}
			// Datapoint one is _overwritten_
			p.metricsChan <- producers.MetricsMessage{
				Name:       "some-message",
				Datapoints: dp1,
				Timestamp:  time.Now().UTC().Unix(),
			}
			time.Sleep(250 * time.Millisecond)

			So(p.store.Size(), ShouldEqual, 2)
		})

		Convey("Should overwrite metrics with the same name and tags", func() {
			port, err := getEphemeralPort()
			if err != nil {
				panic(err)
			}

			p := producerImpl{
				config:             Config{Port: port},
				store:              store.New(),
				metricsChan:        make(chan producers.MetricsMessage),
				janitorRunInterval: 60 * time.Second,
			}
			go p.Run()
			time.Sleep(1 * time.Second)

			dp1 := []producers.Datapoint{
				producers.Datapoint{
					Name: "datapoint",
					Tags: map[string]string{"foo": "bar", "baz": "qux"},
				},
			}
			dp2 := []producers.Datapoint{
				producers.Datapoint{
					Name: "datapoint",
					Tags: map[string]string{"baz": "qux", "foo": "bar"},
				},
			}

			p.metricsChan <- producers.MetricsMessage{
				Name:       "some-message",
				Datapoints: dp1,
				Timestamp:  time.Now().UTC().Unix(),
			}
			time.Sleep(50 * time.Millisecond)
			p.metricsChan <- producers.MetricsMessage{
				Name:       "some-message",
				Datapoints: dp2,
				Timestamp:  time.Now().UTC().Unix(),
			}
			time.Sleep(250 * time.Millisecond)

			So(p.store.Size(), ShouldEqual, 1)
		})

		Convey("Should create a new router on a systemd socket (if it's available)", func() {
			// Mock the systemd socket
			if err := os.Setenv("LISTEN_PID", strconv.Itoa(os.Getpid())); err != nil {
				panic(err)
			}
			if err := os.Setenv("LISTEN_FDS", strconv.Itoa(1)); err != nil {
				panic(err)
			}

			files := activation.Files(false)
			if len(files) != 1 {
				panic(fmt.Errorf("expected activation.Files length to be 1, got %d", len(files)))
			}

			port, err := getEphemeralPort()
			if err != nil {
				panic(err)
			}

			p, _ := New(Config{Port: port})
			go p.Run()
			time.Sleep(1 * time.Second)

			// There shouldn't be anything listening on the configured TCP port
			_, err = net.Dial("tcp", net.JoinHostPort("localhost", strconv.Itoa(port)))
			So(err, ShouldNotBeNil)

			// Restore the environment
			if err := os.Unsetenv("LISTEN_PID"); err != nil {
				panic(err)
			}
			if err := os.Unsetenv("LISTEN_FDS"); err != nil {
				panic(err)
			}
		})

		Convey("Should create a new router on a HTTP port if a systemd socket is unavailable", func() {
			// Ensure the environment is clean
			if err := os.Unsetenv("LISTEN_PID"); err != nil {
				panic(err)
			}
			if err := os.Unsetenv("LISTEN_FDS"); err != nil {
				panic(err)
			}

			port, err := getEphemeralPort()
			if err != nil {
				panic(err)
			}

			p, _ := New(Config{Port: port})
			go p.Run()
			time.Sleep(1 * time.Second)

			// The web server should be listening on the configured TCP port
			_, err = net.Dial("tcp", net.JoinHostPort("localhost", strconv.Itoa(port)))
			So(err, ShouldBeNil)
		})
	})
}

func TestJanitor(t *testing.T) {
	Convey("When running the janitor to clean up stale store objects", t, func() {
		// In this test, we set the default CacheExpiry to 120 seconds, which is
		// far longer than this test should ever take to run. testTime then uses
		// the current time (since janitor uses time.Since() to calculate age),
		// so we need to substract seconds from objects in the store
		// (via producers.MetricsMessage.Timestamp) to simulate "stale" objects.
		p := producerImpl{
			config: Config{
				CacheExpiry: 60 * time.Second,
			},
			store:              store.New(),
			janitorRunInterval: 1 * time.Second,
		}
		testTime := time.Now().UTC().Unix()
		testCases := map[string]interface{}{
			"obj0": producers.MetricsMessage{Timestamp: testTime + 30}, // superfresh, expires in 90secs
			"obj1": producers.MetricsMessage{Timestamp: testTime},      // fresh, expires in 60secs
			"obj2": producers.MetricsMessage{Timestamp: testTime - 57}, // fresh, expires in 3secs
			"obj3": producers.MetricsMessage{Timestamp: testTime - 90}, // stale, expired 30secs ago
			"obj4": producers.MetricsMessage{Timestamp: testTime - 90}, // stale, expired 30secs ago
		}
		p.store.Supplant(testCases)

		go p.janitor()
		time.Sleep(2 * p.janitorRunInterval)

		Convey("Should remove only stale objects", func() {
			So(p.store.Size(), ShouldEqual, 3)
			So(p.store.Objects(), ShouldNotContainKey, "obj3")
			So(p.store.Objects(), ShouldNotContainKey, "obj4")
			So(p.store.Objects(), ShouldContainKey, "obj0")
			So(p.store.Objects(), ShouldContainKey, "obj1")
			So(p.store.Objects(), ShouldContainKey, "obj2")
		})

		Convey("Should run on set schedule", func() {
			// Considering that obj2 should expire in 3 seconds, there's a
			// problem if it hasn't been removed in 5 seconds.
			for i := 0; i < 5; i++ {
				if p.store.Size() == 2 {
					break
				}
				time.Sleep(1 * time.Second)
			}
			So(p.store.Size(), ShouldEqual, 2)
			So(p.store.Objects(), ShouldNotContainKey, "obj2")
		})
	})
}
