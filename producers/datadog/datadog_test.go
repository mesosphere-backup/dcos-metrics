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

package datadog

import (
	"fmt"
	"net"
	"strconv"
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

func TestNew(t *testing.T) {
	Convey("When creating a new instance of the DataDog producer", t, func() {
		Convey("Should return a new Collector instance", func() {
			pi, pc := New(Config{})
			So(pi, ShouldHaveSameTypeAs, &producerImpl{})
			So(pc, ShouldHaveSameTypeAs, make(chan producers.MetricsMessage))
		})
	})
}

func TestRun(t *testing.T) {
	port, err := getEphemeralPort()
	if err != nil {
		panic(err)
	}
	addr, err := net.ResolveUDPAddr("udp", net.JoinHostPort("127.0.0.1", strconv.Itoa(port)))
	if err != nil {
		panic(err)
	}
	ln, err := net.ListenUDP("udp", addr)
	if err != nil {
		panic(err)
	}
	defer ln.Close()

	Convey("When running the DataDog producer", t, func() {
		dog, dogChan := New(Config{Host: "127.0.0.1", Port: port, RetryInterval: 1 * time.Second})
		go dog.Run()
		time.Sleep(1 * time.Second)

		dogChan <- producers.MetricsMessage{
			Name: "dcos.metrics.node",
			Datapoints: []producers.Datapoint{
				producers.Datapoint{
					Name:  "some-metric",
					Value: 123,
				},
			},
			Dimensions: producers.Dimensions{
				ClusterID: "some-cluster",
				MesosID:   "some-mesos-id",
			},
		}

		buf := make([]byte, 1024)
		n, err := ln.Read(buf)
		if err != nil {
			panic(err)
		}
		fmt.Printf("read %d bytes", n)
		data := buf[:n]

		So(n, ShouldBeGreaterThan, 0)
		So(string(data), ShouldEqual, "dcos.metrics.node.some-metric:123.000000|g|#mesos_id:some-mesos-id,cluster_id:some-cluster")
	})
}

func TestBuildTags(t *testing.T) {
	testCases := []struct {
		testCase producers.Dimensions
		expected []string
	}{
		{
			producers.Dimensions{
				MesosID:   "some-mesos-id",
				ClusterID: "some-cluster-id",
				Labels: map[string]string{
					"USER_LABEL_1": "SOME_VAL",
					"USER_LABEL_2": "SOME_OTHER_VAL",
					"UserLabel3":   "SomeCaseSensitiveVal",
				},
			},
			[]string{
				"mesos_id:some-mesos-id",
				"cluster_id:some-cluster-id",
				"USER_LABEL_1:SOME_VAL",
				"USER_LABEL_2:SOME_OTHER_VAL",
				"UserLabel3:SomeCaseSensitiveVal",
			},
		},
	}

	Convey("When building DataDog tags from a Dimensions struct", t, func() {
		Convey("Should return a slice of strings as required by the DataDog library", func() {
			for _, tc := range testCases {
				tags, err := buildTags(tc.testCase)
				So(tags, ShouldResemble, tc.expected)
				So(err, ShouldBeNil)
			}
		})
	})
}

// getEphemeralPort returns an available ephemeral port on the system.
func getEphemeralPort() (int, error) {
	addr, err := net.ResolveUDPAddr("udp", "localhost:0")
	if err != nil {
		return 0, err
	}

	l, err := net.ListenUDP("udp", addr)
	if err != nil {
		return 0, err
	}
	defer l.Close()

	return l.LocalAddr().(*net.UDPAddr).Port, nil
}
