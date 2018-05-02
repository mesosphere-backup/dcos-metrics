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
	"os"
	"os/signal"
	"strconv"
	"syscall"

	"github.com/atlassian/gostatsd"
	astatsd "github.com/atlassian/gostatsd/pkg/statsd"
	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
	"golang.org/x/time/rate"
)

// Collector is the statsd metrics collector.
type Collector struct {
	MetricsChan chan producers.MetricsMessage
	ServerPort  int `yaml:"server_port,omitempty"`
	nodeInfo    collectors.NodeInfo
}

// New starts a statsd server on the port specified in its configuration. It
// returns a channel which will carry metrics messages.
func New(port int, ctx context.Context) Collector {
	backend := &Collector{
		MetricsChan: make(chan producers.MetricsMessage),
		ServerPort:  port,
	}

	s := astatsd.Server{
		Backends:         []gostatsd.Backend{backend},
		MetricsAddr:      fmt.Sprintf(":%d", port),
		Limiter:          rate.NewLimiter(astatsd.DefaultMaxCloudRequests, astatsd.DefaultBurstCloudRequests),
		DefaultTags:      astatsd.DefaultTags,
		ExpiryInterval:   astatsd.DefaultExpiryInterval,
		FlushInterval:    astatsd.DefaultFlushInterval,
		MaxReaders:       astatsd.DefaultMaxReaders,
		MaxParsers:       astatsd.DefaultMaxParsers,
		MaxWorkers:       astatsd.DefaultMaxWorkers,
		MaxQueueSize:     astatsd.DefaultMaxQueueSize,
		EstimatedTags:    astatsd.DefaultEstimatedTags,
		PercentThreshold: astatsd.DefaultPercentThreshold,
		HeartbeatEnabled: astatsd.DefaultHeartbeatEnabled,
		ReceiveBatchSize: astatsd.DefaultReceiveBatchSize,
		StatserType:      astatsd.StatserInternal,
		CacheOptions: astatsd.CacheOptions{
			CacheRefreshPeriod:        astatsd.DefaultCacheRefreshPeriod,
			CacheEvictAfterIdlePeriod: astatsd.DefaultCacheEvictAfterIdlePeriod,
			CacheTTL:                  astatsd.DefaultCacheTTL,
			CacheNegativeTTL:          astatsd.DefaultCacheNegativeTTL,
		},
	}

	sf, addr := socketFactory(s.MetricsAddr)

	go func() {
		err := s.RunWithCustomSocket(ctx, sf)
		if err != nil && err != context.Canceled && err != context.DeadlineExceeded {
			fmt.Errorf("statsd server run failed: %v", err)
		}
	}()

	// extract the port from the net address
	_, addrPort, _ := net.SplitHostPort(addr.String())
	p, _ := strconv.Atoi(addrPort)
	backend.ServerPort = p

	return *backend
}

// socketFactory mimics the Atlassian statsd implementation's socketFactory,
// but also exposes the address on which the server is listening.
// This is useful in case we opened a connection on port 0 and need to know
// which port the OS allocated.
func socketFactory(metricsAddr string) (astatsd.SocketFactory, net.Addr) {
	conn, err := net.ListenPacket("udp", metricsAddr)
	return func() (net.PacketConn, error) {
		return conn, err
	}, conn.LocalAddr()
}

// CancelOnInterrupt calls f when os.Interrupt or SIGTERM is received.
func CancelOnInterrupt(ctx context.Context, f context.CancelFunc) {
	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)
	go func() {
		select {
		case <-ctx.Done():
		case <-c:
			f()
		}
	}()
}

func (c *Collector) Name() string {
	return "dcosMetricsCollector"
}

func (c *Collector) SendMetricsAsync(ctx context.Context, m *gostatsd.MetricMap, callback gostatsd.SendCallback) {
	// TODO manage timers, counters and sets
	dd := []producers.Datapoint{}
	m.Gauges.Each(func(name, tagset string, g gostatsd.Gauge) {
		dd = append(dd, producers.Datapoint{
			Name:  name,
			Value: g.Value,
		})
	})

	if len(dd) > 0 {
		c.MetricsChan <- producers.MetricsMessage{
			Datapoints: dd,
		}
	}

	callback(nil)
}

func (c *Collector) SendEvent(ctx context.Context, e *gostatsd.Event) error {
	fmt.Println(e)
	return nil
}
