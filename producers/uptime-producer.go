package main

import (
	"fmt"
	"log"
	"os/exec"
	"time"

	kafka "github.com/Shopify/sarama"
)

func getLocalProducer() (kafka.AsyncProducer, error) {
	kafkaConfig := kafka.NewConfig()
	kafkaConfig.Producer.RequiredAcks = kafka.WaitForLocal
	kafkaConfig.Producer.Return.Successes = true

	kafkaProducer, err := kafka.NewAsyncProducer([]string{"localhost:9092"}, kafkaConfig)
	if err != nil {
		return kafkaProducer, err
	}

	go func() {
		for err := range kafkaProducer.Errors() {
			log.Println("Failed to write metric: ", err)
		}
	}()

	go func() {
		for yay := range kafkaProducer.Successes() {
			if bytes, err := yay.Value.Encode(); err != nil {
				fmt.Println("ERROR ", err)
			} else {
				fmt.Println("SUCCESS: ", string(bytes))
			}
		}
	}()

	return kafkaProducer, nil
}

func getUptime() ([]byte, error) {
	return exec.Command("uptime").Output()
}

func main() {
	// Get the producer and defer a close() until after main() exits.
	producer, err := getLocalProducer()
	if err != nil {
		panic(err)
	}

	defer func() {
		if err := producer.Close(); err != nil {
			fmt.Println(err)
		}
	}()

	for {
		time.Sleep(time.Duration(1) * time.Second)
		if uptime, err := getUptime(); err == nil {
			fmt.Printf("Sending: %s", string(uptime))
			producer.Input() <- &kafka.ProducerMessage{
				Topic: "host-uptime",
				Value: kafka.ByteEncoder(uptime),
				Key:   kafka.ByteEncoder("uptime"),
			}

		} else {
			panic(err)
		}
	}
}
