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

	"github.com/shirou/gopsutil/cpu"
	. "github.com/smartystreets/goconvey/convey"
)

var (
	mockNodeMetrics = nodeMetrics{
		Uptime:          uint64(7865),
		ProcessCount:    3,
		NumCores:        4,
		Load1Min:        float64(0.42),
		Load5Min:        float64(0.58),
		Load15Min:       float64(0.57),
		CPUTotalPct:     float64(12.50),
		CPUUserPct:      float64(9.60),
		CPUSystemPct:    float64(2.90),
		CPUIdlePct:      float64(87.40),
		CPUWaitPct:      float64(0.10),
		MemTotalBytes:   uint64(16310888 * 1024),
		MemFreeBytes:    uint64(9121592 * 1024),
		MemBuffersBytes: uint64(101784 * 1024),
		MemCachedBytes:  uint64(2909412 * 1024),
		SwapTotalBytes:  uint64(16658428 * 1024),
		SwapFreeBytes:   uint64(16658421 * 1024),
		SwapUsedBytes:   uint64(1 * 1024),
		Filesystems: []nodeFilesystem{
			nodeFilesystem{
				Name:               "/",
				CapacityTotalBytes: uint64(449660412 * 1024),
				CapacityUsedBytes:  uint64(25819220 * 1024),
				CapacityFreeBytes:  uint64(423841192 * 1024),
			},
			nodeFilesystem{
				Name:               "/boot",
				CapacityTotalBytes: uint64(458961 * 1024),
				CapacityUsedBytes:  uint64(191925 * 1024),
				CapacityFreeBytes:  uint64(267036 * 1024),
			},
		},
		NetworkInterfaces: []nodeNetworkInterface{
			nodeNetworkInterface{
				Name:      "eth0",
				RxBytes:   uint64(260522667),
				TxBytes:   uint64(10451619),
				RxPackets: uint64(1058595),
				TxPackets: uint64(62547),
				RxDropped: uint64(99),
				TxDropped: uint64(99),
				RxErrors:  uint64(99),
				TxErrors:  uint64(99),
			},
			nodeNetworkInterface{
				Name:      "lo",
				RxBytes:   uint64(7939933),
				TxBytes:   uint64(8139647),
				RxPackets: uint64(133276),
				TxPackets: uint64(133002),
				RxDropped: uint64(88),
				TxDropped: uint64(88),
				RxErrors:  uint64(88),
				TxErrors:  uint64(88),
			},
		},
	}
)

func TestGetNodeMetrics(t *testing.T) {
	Convey("When getting node metrics, should return nodeMetrics type", t, func() {
		m, err := getNodeMetrics()
		So(err, ShouldBeNil)
		So(m, ShouldHaveSameTypeAs, nodeMetrics{})

		// smoke test certain values that should always be > 0
		So(m.Uptime, ShouldBeGreaterThan, 0)
		So(m.ProcessCount, ShouldBeGreaterThan, 0)
		So(m.NumCores, ShouldBeGreaterThan, 0)
		So(m.MemTotalBytes, ShouldBeGreaterThan, 0)
		So(len(m.Filesystems), ShouldBeGreaterThan, 0)
		So(len(m.NetworkInterfaces), ShouldBeGreaterThan, 0)
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
