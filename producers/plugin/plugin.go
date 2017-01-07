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
type metricsServerImpl struct {
	metricsChan chan producers.MetricsMessage
}

// TODO(malnick) AttachOutputStream should eventually implement a gRPC stream
// type return. For now we are still testing functionality.
func (s *metricsServerImpl) AttachOutputStream(metricType *pb.MetricsCollectorType, server pb.Metrics_AttachOutputStreamServer) error {

	metricCollectorType := fmt.Sprintf("dcos.metrics.%s", metricType.Type)

	for {
		select {
		case metricReceived := <-s.metricsChan:
			if metricReceived.Name == metricCollectorType {
				metricMessage := &pb.MetricsMessage{
					Name: metricReceived.Name,
					Dimensions: &pb.Dimensions{
						MesosID:            metricReceived.Dimensions.MesosID,
						ClusterID:          metricReceived.Dimensions.ClusterID,
						ContainerID:        metricReceived.Dimensions.ContainerID,
						ExecutorID:         metricReceived.Dimensions.ExecutorID,
						FrameworkName:      metricReceived.Dimensions.FrameworkName,
						FrameworkID:        metricReceived.Dimensions.FrameworkID,
						FrameworkRole:      metricReceived.Dimensions.FrameworkRole,
						FrameworkPrincipal: metricReceived.Dimensions.FrameworkPrincipal,
						Hostname:           metricReceived.Dimensions.Hostname,
						Labels:             metricReceived.Dimensions.Labels,
					},
				}

				for _, datapoint := range metricReceived.Datapoints {
					metricMessage.Datapoints = append(
						metricMessage.Datapoints,
						&pb.Datapoint{
							Timestamp: datapoint.Timestamp,
							Name:      datapoint.Name,
							Value:     fmt.Sprintf("%v", datapoint.Value),
						})
				}

				err := server.Send(metricMessage)
				return err
			}
			return fmt.Errorf("No metrics found for %s collector type.", metricCollectorType)
		default:
			return fmt.Errorf("No metrics available")
		}
	}
	return nil
}

func New(cfg Config) (producers.MetricsProducer, chan producers.MetricsMessage) {
	p := producerImpl{
		config:      cfg,
		metricsChan: make(chan producers.MetricsMessage),
	}
	return &p, p.metricsChan
}

func (p *producerImpl) Run() error {
	plugLog.Info("Starting plugin gRPC/TCP listening service")
	listener, err := net.Listen("tcp", fmt.Sprintf(":%d", p.config.Port))
	if err != nil {
		plugLog.Fatalf("Failed to start TCP listening, %v", err)
	}
	s := grpc.NewServer()
	pb.RegisterMetricsServer(s, &metricsServerImpl{
		metricsChan: p.metricsChan,
	})
	reflection.Register(s)
	if err := s.Serve(listener); err != nil {
		plugLog.Fatalf("Failed to serve, %v", err)
	}
	return nil
}
