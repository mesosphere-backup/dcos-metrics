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
	"github.com/dcos/dcos-metrics/producers"
	"github.com/shirou/gopsutil/net"
)

const (
	/* network.<namespace> */
	NET_IN          = "network.in"
	NET_OUT         = "network.out"
	NET_IN_PACKETS  = "network.in.packets"
	NET_OUT_PACKETS = "network.out.packets"
	NET_IN_DROPPED  = "network_in_dropped"
	NET_OUT_DROPPED = "network_out_dropped"
	NET_IN_ERRORS   = "network_in_errors"
	NET_OUT_ERRORS  = "network_out_errors"
)

type networkMetrics struct {
	interfaces []networkMetric
}

type networkMetric struct {
	interfaceName string
	netIn         uint64
	netOut        uint64
	netInPackets  uint64
	netOutPackets uint64
	netInDropped  uint64
	netOutDropped uint64
	netInErrors   uint64
	netOutErrors  uint64
	timestamp     string
}

func (m *networkMetrics) poll() error {
	netMetrics := []networkMetric{}

	ts := thisTime()
	netInterface, err := net.IOCounters(true) // per network interface
	if err != nil {
		return err
	}

	for _, nic := range netInterface {
		netMetrics = append(netMetrics, networkMetric{
			interfaceName: nic.Name,
			netIn:         nic.BytesRecv,
			netOut:        nic.BytesSent,
			netInPackets:  nic.PacketsRecv,
			netOutPackets: nic.PacketsSent,
			netInDropped:  nic.Dropin,
			netOutDropped: nic.Dropout,
			netInErrors:   nic.Errin,
			netOutErrors:  nic.Errout,
			timestamp:     ts,
		})
	}

	m.interfaces = netMetrics

	return nil
}

func (m *networkMetrics) getDatapoints() ([]producers.Datapoint, error) {
	var ncDps []producers.Datapoint
	for _, nic := range m.interfaces {
		ncDps = append(ncDps, producers.Datapoint{
			Name:      NET_IN,
			Unit:      BYTES,
			Value:     nic.netIn,
			Timestamp: nic.timestamp,
			Tags: map[string]string{
				"interface": nic.interfaceName,
			},
		})

		ncDps = append(ncDps, producers.Datapoint{
			Name:      NET_OUT,
			Unit:      BYTES,
			Value:     nic.netOut,
			Timestamp: nic.timestamp,
			Tags: map[string]string{
				"interface": nic.interfaceName,
			},
		})

		ncDps = append(ncDps, producers.Datapoint{
			Name:      NET_IN_PACKETS,
			Unit:      COUNT,
			Value:     nic.netInPackets,
			Timestamp: nic.timestamp,
			Tags: map[string]string{
				"interface": nic.interfaceName,
			},
		})

		ncDps = append(ncDps, producers.Datapoint{
			Name:      NET_OUT_PACKETS,
			Unit:      COUNT,
			Value:     nic.netOutPackets,
			Timestamp: nic.timestamp,
			Tags: map[string]string{
				"interface": nic.interfaceName,
			},
		})

		ncDps = append(ncDps, producers.Datapoint{
			Name:      NET_IN_DROPPED,
			Unit:      COUNT,
			Value:     nic.netInDropped,
			Timestamp: nic.timestamp,
			Tags: map[string]string{
				"interface": nic.interfaceName,
			},
		})

		ncDps = append(ncDps, producers.Datapoint{
			Name:      NET_OUT_DROPPED,
			Unit:      COUNT,
			Value:     nic.netOutDropped,
			Timestamp: nic.timestamp,
			Tags: map[string]string{
				"interface": nic.interfaceName,
			},
		})

		ncDps = append(ncDps, producers.Datapoint{
			Name:      NET_IN_ERRORS,
			Unit:      COUNT,
			Value:     nic.netInErrors,
			Timestamp: nic.timestamp,
			Tags: map[string]string{
				"interface": nic.interfaceName,
			},
		})

		ncDps = append(ncDps, producers.Datapoint{
			Name:      NET_OUT_ERRORS,
			Unit:      COUNT,
			Value:     nic.netOutErrors,
			Timestamp: nic.timestamp,
			Tags: map[string]string{
				"interface": nic.interfaceName,
			},
		})
	}

	return ncDps, nil
}
