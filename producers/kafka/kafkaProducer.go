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

package kafka

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"strings"
	"time"

	"github.com/Shopify/sarama"
	"github.com/dcos/dcos-metrics/events"
	util "github.com/dcos/dcos-metrics/util"
)

// KafkaConfig{} is a set of configurations for Kafka Producer
// NOTE:
// 	KafkaFramework overrides Brokers. This is an artifact from the migration
// 	process, in the future we'll only have a broker enabled here instead of passing
// 	framework.
type KafkaConfig struct {
	Brokers            string `yaml:"brokers"`
	KafkaFramework     string `yaml:"kafka_framework"`
	FlushPeriod        int    `yaml:"flush_period"`
	CompressionEnabled bool   `yaml:"snappy_compression_enabled"`
	AllAcksEnabled     bool   `yaml:"all_acks_enabled"`
	Verbose            bool   `yaml:"verbose_kafka"`
}

// KafkaMessage ...
type KafkaMessage struct {
	Topic string
	Data  []byte
}

// RunKafkaProducer creates and runs a Kafka Producer which sends records passed to the provided channel.
// This function should be run as a gofunc.
func RunKafkaProducer(messages <-chan KafkaMessage, stats chan<- events.StatsEvent, kc KafkaConfig) error {
	if kc.Verbose {
		sarama.Logger = log.New(os.Stdout, "[sarama] ", log.LstdFlags)
	}

	for {
		producer, err := kafkaProducer(stats, kc)
		if err != nil {
			stats <- events.MakeEvent(events.KafkaConnectionFailed)
			log.Println("Failed to open Kafka producer:", err)
			// reuse flush period as the retry delay:
			log.Printf("Waiting for %dms\n", kc.FlushPeriod)
			time.Sleep(time.Duration(kc.FlushPeriod) * time.Millisecond)
			continue
		}
		stats <- events.MakeEvent(events.KafkaSessionOpened)
		defer func() {
			stats <- events.MakeEvent(events.KafkaSessionClosed)
			if err := producer.Close(); err != nil {
				log.Println("Failed to shut down producer cleanly:", err)
			}
		}()
		for {
			message := <-messages
			producer.Input() <- &sarama.ProducerMessage{
				Topic: message.Topic,
				Value: sarama.ByteEncoder(message.Data),
			}
			stats <- events.MakeEventSuff(events.KafkaMessageSent, message.Topic)
		}
	}
}

// ---

func kafkaProducer(stats chan<- events.StatsEvent, kc KafkaConfig) (kafkaProducer sarama.AsyncProducer, err error) {
	var brokers []string
	if len(kc.Brokers) != 0 {
		brokers = strings.Split(kc.Brokers, ",")
		if len(brokers) == 0 {
			log.Fatal("-kafka-brokers must be non-empty.")
		}
	} else if len(kc.KafkaFramework) != 0 {
		foundBrokers, err := lookupBrokers(kc.KafkaFramework)
		if err != nil {
			stats <- events.MakeEventSuff(events.KafkaLookupFailed, kc.KafkaFramework)
			return nil, fmt.Errorf("Broker lookup against framework %s failed: %s", kc.KafkaFramework, err)
		}
		brokers = append(brokers, foundBrokers...)
	} else {
		flag.Usage()
		log.Fatal("Either -kafka-framework or -kafka-brokers must be specified, or -kafka must be 'false'.")
	}
	log.Println("Kafka brokers:", strings.Join(brokers, ", "))

	kafkaProducer, err = newAsyncProducer(brokers, kc)
	if err != nil {
		return nil, fmt.Errorf("Producer creation against brokers %+v failed: %s", brokers, err)
	}
	return kafkaProducer, nil
}

func newAsyncProducer(brokerList []string, kc KafkaConfig) (producer sarama.AsyncProducer, err error) {
	// For the access log, we are looking for AP semantics, with high throughput.
	// By creating batches of compressed messages, we reduce network I/O at a cost of more latency.
	config := sarama.NewConfig()
	if kc.AllAcksEnabled {
		config.Producer.RequiredAcks = sarama.WaitForAll
	} else {
		config.Producer.RequiredAcks = sarama.WaitForLocal
	}
	if kc.CompressionEnabled {
		config.Producer.Compression = sarama.CompressionSnappy
	} else {
		config.Producer.Compression = sarama.CompressionNone
	}
	config.Producer.Flush.Frequency = time.Duration(kc.FlushPeriod) * time.Millisecond

	producer, err = sarama.NewAsyncProducer(brokerList, config)
	if err != nil {
		return nil, err
	}
	go func() {
		for err := range producer.Errors() {
			log.Println("Failed to write metrics to Kafka:", err)
		}
	}()
	return producer, nil
}

// ---

// Returns a list of broker endpoints, each of the form "host:port"
func lookupBrokers(framework string) (brokers []string, err error) {
	schedulerEndpoint, err := connectionEndpoint(framework)
	if err != nil {
		return nil, err
	}
	body, err := util.HTTPGet(schedulerEndpoint)
	if err != nil {
		return nil, err
	}
	return extractBrokers(body)
}

func connectionEndpoint(framework string) (endpoint string, err error) {
	// Perform SRV lookup to get scheduler's port number:
	// "_<framework>._tcp.marathon.mesos."
	_, addrs, err := net.LookupSRV(framework, "tcp", "marathon.mesos")
	if err != nil {
		return "", err
	}
	if len(addrs) == 0 {
		return "", fmt.Errorf("Framework '%s' not found", framework)
	}
	url := fmt.Sprintf("http://%s.mesos:%d/v1/connection", framework, addrs[0].Port)
	log.Println("Fetching broker list from Kafka Framework at:", url)
	return url, nil
}

func extractBrokers(body []byte) (brokers []string, err error) {
	var jsonData map[string]interface{}
	if err = json.Unmarshal(body, &jsonData); err != nil {
		return nil, err
	}
	// expect "dns" entry containing a list of strings
	jsonBrokers := jsonData["dns"].([]interface{})
	brokers = make([]string, len(jsonBrokers))
	for i, jsonDNSEntry := range jsonBrokers {
		brokers[i] = jsonDNSEntry.(string)
	}
	return brokers, nil
}
