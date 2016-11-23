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
	"math"
	"sync"

	"github.com/shirou/gopsutil/cpu"
	"github.com/shirou/gopsutil/disk"
	"github.com/shirou/gopsutil/host"
	"github.com/shirou/gopsutil/load"
	"github.com/shirou/gopsutil/mem"
	"github.com/shirou/gopsutil/net"
)

type nodeMetrics struct {
	Uptime       uint64 `json:"uptime"`
	ProcessCount uint64 `json:"processes"`

	NumCores  int32   `json:"cpu.cores"`
	Load1Min  float64 `json:"load.1min"`
	Load5Min  float64 `json:"load.5min"`
	Load15Min float64 `json:"load.15min"`

	CPUTotalPct  float64 `json:"cpu.total"`
	CPUUserPct   float64 `json:"cpu.user"`
	CPUSystemPct float64 `json:"cpu.system"`
	CPUIdlePct   float64 `json:"cpu.idle"`
	CPUWaitPct   float64 `json:"cpu.wait"`

	MemTotalBytes   uint64 `json:"memory.total"`
	MemFreeBytes    uint64 `json:"memory.free"`
	MemBuffersBytes uint64 `json:"memory.buffers"`
	MemCachedBytes  uint64 `json:"memory.cached"`

	SwapTotalBytes uint64 `json:"swap.total"`
	SwapFreeBytes  uint64 `json:"swap.free"`
	SwapUsedBytes  uint64 `json:"swap.used"`

	Filesystems       []nodeFilesystem       `json:"filesystems"`
	NetworkInterfaces []nodeNetworkInterface `json:"network_interfaces"`
}

type nodeFilesystem struct {
	Name               string `json:"name"`
	CapacityTotalBytes uint64 `json:"filesystem.{{.Name}}.capacity.total"`
	CapacityUsedBytes  uint64 `json:"filesystem.{{.Name}}.capacity.used"`
	CapacityFreeBytes  uint64 `json:"filesystem.{{.Name}}.capacity.free"`
	InodesTotal        uint64 `json:"filesystem.{{.Name}}.inodes.total"`
	InodesUsed         uint64 `json:"filesystem.{{.Name}}.inodes.used"`
	InodesFree         uint64 `json:"filesystem.{{.Name}}.inodes.free"`
}

type nodeNetworkInterface struct {
	Name      string `json:"name"`
	RxBytes   uint64 `json:"network.{{.Name}}.in.bytes"`
	TxBytes   uint64 `json:"network.{{.Name}}.out.bytes"`
	RxPackets uint64 `json:"network.{{.Name}}.in.packets"`
	TxPackets uint64 `json:"network.{{.Name}}.out.packets"`
	RxDropped uint64 `json:"network.{{.Name}}.in.dropped"`
	TxDropped uint64 `json:"network.{{.Name}}.out.dropped"`
	RxErrors  uint64 `json:"network.{{.Name}}.in.errors"`
	TxErrors  uint64 `json:"network.{{.Name}}.out.errors"`
}

func (h *NodeCollector) getNodeMetrics() (nodeMetrics, error) {
	l := getLoadAvg()
	cpuStatePcts := calculatePcts(getCPUTimes())
	m := getMemory()
	s := getSwap()

	return nodeMetrics{
		Uptime:       getUptime(),
		ProcessCount: getProcessCount(),

		NumCores:  getNumCores(),
		Load1Min:  l.Load1,
		Load5Min:  l.Load5,
		Load15Min: l.Load15,

		// Percentages are calculated between the last poll of the CPU times
		// and the current times, as stored in lastCPU. 100.00 => 100.00%
		CPUTotalPct:  cpuStatePcts.User + cpuStatePcts.System,
		CPUUserPct:   cpuStatePcts.User,
		CPUSystemPct: cpuStatePcts.System,
		CPUIdlePct:   cpuStatePcts.Idle,
		CPUWaitPct:   cpuStatePcts.Iowait,

		MemTotalBytes:   m.Total,
		MemFreeBytes:    m.Free,
		MemBuffersBytes: m.Buffers,
		MemCachedBytes:  m.Cached,

		SwapTotalBytes: s.Total,
		SwapFreeBytes:  s.Free,
		SwapUsedBytes:  s.Used,

		Filesystems:       getFilesystems(),
		NetworkInterfaces: getNetworkInterfaces(),
	}, nil
}

// -- helpers

type lastCPUTimes struct {
	sync.Mutex
	times cpu.TimesStat
}

var lastCPU lastCPUTimes

func init() {
	t, _ := cpu.Times(false) // get totals, not per-cpu stats
	lastCPU.Lock()
	lastCPU.times = t[0]
	lastCPU.Unlock()
}

func getUptime() uint64 {
	uptime, _ := host.Uptime()
	return uptime
}

func getProcessCount() uint64 {
	i, _ := host.Info()
	return i.Procs
}

func getNumCores() int32 {
	cores := int32(0)
	cpus, _ := cpu.Info()
	for _, c := range cpus {
		cores += c.Cores
	}
	return cores
}

func getLoadAvg() *load.AvgStat {
	l, _ := load.Avg()
	return l
}

func getCPUTimes() (cpu.TimesStat, cpu.TimesStat) {
	currentTimes, _ := cpu.Times(false) // get totals, not per-cpu stats
	lastTimes := lastCPU.times

	lastCPU.Lock()
	lastCPU.times = currentTimes[0] // update lastTimes to the currentTimes
	lastCPU.Unlock()

	return currentTimes[0], lastTimes
}

func getMemory() *mem.VirtualMemoryStat {
	m, _ := mem.VirtualMemory()
	return m
}

func getSwap() *mem.SwapMemoryStat {
	s, _ := mem.SwapMemory()
	return s
}

func getFilesystems() []nodeFilesystem {
	f := []nodeFilesystem{}
	parts, _ := disk.Partitions(false) // only physical partitions
	for _, part := range parts {
		usage, _ := disk.Usage(part.Mountpoint)
		f = append(f, nodeFilesystem{
			Name:               usage.Path,
			CapacityTotalBytes: usage.Total,
			CapacityUsedBytes:  usage.Used,
			CapacityFreeBytes:  usage.Free,
			InodesTotal:        usage.InodesTotal,
			InodesUsed:         usage.InodesUsed,
			InodesFree:         usage.InodesFree,
		})
	}
	return f
}

func getNetworkInterfaces() []nodeNetworkInterface {
	n := []nodeNetworkInterface{}
	ioc, _ := net.IOCounters(true) // per nic
	for _, nic := range ioc {
		n = append(n, nodeNetworkInterface{
			Name:      nic.Name,
			RxBytes:   nic.BytesRecv,
			TxBytes:   nic.BytesSent,
			RxPackets: nic.PacketsRecv,
			TxPackets: nic.PacketsSent,
			RxDropped: nic.Dropin,
			TxDropped: nic.Dropout,
			RxErrors:  nic.Errin,
			TxErrors:  nic.Errout,
		})
	}
	return n
}

// calculatePct returns the percent utilization for CPU states. 100.00 => 100.00%
func calculatePcts(lastTimes cpu.TimesStat, curTimes cpu.TimesStat) cpu.TimesStat {
	totalDelta := curTimes.Total() - lastTimes.Total()
	if totalDelta == 0 {
		totalDelta = 1 // can't divide by zero
	}
	return cpu.TimesStat{
		User:      round(math.Dim(curTimes.User, lastTimes.User) / totalDelta * 100),
		System:    round(math.Dim(curTimes.System, lastTimes.System) / totalDelta * 100),
		Idle:      round(math.Dim(curTimes.Idle, lastTimes.Idle) / totalDelta * 100),
		Nice:      round(math.Dim(curTimes.Nice, lastTimes.Nice) / totalDelta * 100),
		Iowait:    round(math.Dim(curTimes.Iowait, lastTimes.Iowait) / totalDelta * 100),
		Irq:       round(math.Dim(curTimes.Irq, lastTimes.Irq) / totalDelta * 100),
		Softirq:   round(math.Dim(curTimes.Softirq, lastTimes.Softirq) / totalDelta * 100),
		Steal:     round(math.Dim(curTimes.Steal, lastTimes.Steal) / totalDelta * 100),
		Guest:     round(math.Dim(curTimes.Guest, lastTimes.Guest) / totalDelta * 100),
		GuestNice: round(math.Dim(curTimes.GuestNice, lastTimes.GuestNice) / totalDelta * 100),
		Stolen:    round(math.Dim(curTimes.Stolen, lastTimes.Stolen) / totalDelta * 100),
	}
}

// Helper function for rounding to two decimal places
func round(f float64) float64 {
	shift := math.Pow(10, float64(2))
	return math.Floor(f*shift+.5) / shift
}
