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

package plugin

import (
	"golang.org/x/net/context"

	"fmt"
	"net"

	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

	log "github.com/Sirupsen/logrus"

	"github.com/dcos/dcos-metrics/producers"

	pb "github.com/dcos/dcos-metrics/producers/plugin/plugin"
)

var plugLog = log.WithFields(log.Fields{
	"producer": "plugin",
})

// Config for plugin producer
type Config struct {
	Port int `yaml:"port"`
}

type producerImpl struct {
	config      Config
	metricsChan chan producers.MetricsMessage
}

// server implements plugin.MetricsServer
type metricsServerImpl struct{}

func (s *metricsServerImpl) AttachOutputStream(ctx context.Context, in *pb.MetricsCollectorType) (*pb.MetricsMessage, error) {
	return &pb.MetricsMessage{
		Message: "metrics!",
	}, nil
}

func New(cfg Config) (producers.MetricsProducer, chan producers.MetricsMessage) {
	p := producerImpl{
		config:      cfg,
		metricsChan: make(chan producers.MetricsMessage),
	}
	return &p, p.metricsChan
}

func (p *producerImpl) Run() error {
	listener, err := net.Listen("tcp", fmt.Sprintf("%d", p.config.Port))
	if err != nil {
		plugLog.Fatalf("Failed to start TCP listening")
	}
	s := grpc.NewServer()
	pb.RegisterMetricsServer(s, &metricsServerImpl{})
	reflection.Register(s)
	if err := s.Serve(listener); err != nil {
		plugLog.Fatalf("Failed to serve, %v", err)
	}
	return nil
}
