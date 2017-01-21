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

package node

import (
	"reflect"
	"testing"
	"time"

	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/cpu"
	. "github.com/smartystreets/goconvey/convey"
)

func TestGetNodeMetrics(t *testing.T) {
	Convey("When getting node metrics, should return nodeMetrics type", t, func() {
		m, err := getNodeMetrics()
		So(err, ShouldBeNil)

		So(len(m), ShouldBeGreaterThan, 0)

		for _, dp := range m {
			So(dp, ShouldHaveSameTypeAs, producers.Datapoint{})
		}
	})
}

func TestCalculatePcts(t *testing.T) {
	lastTimes := cpu.TimesStat{
		CPU:       "cpu-total",
		User:      20564.8,
		System:    5355.2,
		Idle:      3866.2,
		Nice:      1141.0,
		Iowait:    161.4,
		Irq:       0.0,
		Softirq:   138.8,
		Steal:     0.0,
		Guest:     0.0,
		GuestNice: 0.0,
		Stolen:    0.0,
	}
	currentTimes := cpu.TimesStat{
		CPU:       "cpu-total",
		User:      20675.6,
		System:    5388.1,
		Idle:      39568.8,
		Nice:      1141.0,
		Iowait:    164.5,
		Irq:       0.0,
		Softirq:   139.2,
		Steal:     0.0,
		Guest:     0.0,
		GuestNice: 0.0,
		Stolen:    0.0,
	}

	Convey("When calculating CPU state percentages", t, func() {
		pcts := calculatePcts(lastTimes, currentTimes)
		Convey("Percentages should be calculated to two decimal places", func() {
			So(pcts.User, ShouldEqual, 0.31)
			So(pcts.System, ShouldEqual, 0.09)
			So(pcts.Idle, ShouldEqual, 99.59)
			So(pcts.Iowait, ShouldEqual, 0.01)
		})
		Convey("No percentages should be negative", func() {
			v := reflect.ValueOf(pcts)
			for i := 0; i < v.NumField(); i++ {
				if v.Field(i).Kind() == reflect.String {
					continue
				}
				So(v.Field(i).Interface(), ShouldBeGreaterThanOrEqualTo, 0)
			}
		})
	})
}

func TestRound(t *testing.T) {
	Convey("When rounding float64 values to two decimal places", t, func() {
		Convey("Should work on all numbers", func() {
			testCases := []struct {
				input    float64
				expected float64
			}{
				{-123.456, -123.46},
				{123.456, 123.46},
				{0, 0.00},
				{-1, -1.00},
				{100.00000, 100.00},
				{100, 100.00},
			}

			for _, tc := range testCases {
				So(round(tc.input), ShouldEqual, tc.expected)
			}
		})
	})
}

func TestInit(t *testing.T) {
	Convey("When initializing the node metrics collector", t, func() {
		Convey("Should automatically set lastCPU times", func() {
			So(lastCPU.times.CPU, ShouldNotEqual, "")
		})
	})
}

func TestGetCPUTimes(t *testing.T) {
	Convey("When getting CPU times", t, func() {
		Convey("Should return both the current times and last times (so that percentages can be calculated)", func() {
			time.Sleep(1 * time.Second)
			cur, last := getCPUTimes()
			So(cur.User, ShouldBeGreaterThan, last.User)
			So(cur.Idle, ShouldBeGreaterThan, last.Idle)
		})
	})
}

//func TestGetFilesystems(t *testing.T) {
//	Convey("When getting filesystems", t, func() {
//		Convey("Should return a list containing nodeFilesystem{} structs", func() {
//			fs := getFilesystems()
//			So(len(fs), ShouldBeGreaterThan, 0)
//			So(fs, ShouldHaveSameTypeAs, []nodeFilesystem{})
//		})
//	})
//}
//
//func TestGetNetworkInterfaces(t *testing.T) {
//	Convey("When getting network interfaces", t, func() {
//		Convey("Should return a list containing nodeNetworkInterface{} structs", func() {
//			ifs := getNetworkInterfaces()
//			So(len(ifs), ShouldBeGreaterThan, 0)
//			So(ifs, ShouldHaveSameTypeAs, []nodeNetworkInterface{})
//		})
//	})
//}
