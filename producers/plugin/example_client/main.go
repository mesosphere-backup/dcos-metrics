package main

import (
	"log"

	pb "github.com/dcos/dcos-metrics/producers/plugin/plugin"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
)

const (
	address = "localhost:9001"
)

func main() {
	// Set up a connection to the server.
	conn, err := grpc.Dial(address, grpc.WithInsecure())
	if err != nil {
		log.Fatalf("did not connect: %v", err)
	}
	defer conn.Close()
	c := pb.NewMetricsClient(conn)

	r, err := c.AttachOutputStream(context.Background(), &pb.MetricsCollectorType{
		Type: "foo",
	})
	if err != nil {
		log.Fatalf("Could not get metrics: %v", err)
	}
	log.Printf("Metric: %s", r.Message)
}
