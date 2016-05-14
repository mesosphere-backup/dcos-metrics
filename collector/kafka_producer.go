package collector

import (
	"errors"
	"flag"
	"fmt"
	"github.com/Shopify/sarama"
	"log"
	"os"
	"strings"
	"time"
)

var (
	brokersFlag = flag.String("brokers", os.Getenv("KAFKA_PEERS"),
		"The Kafka brokers to connect to, as a comma separated list.")
	frameworkFlag = flag.String("framework", EnvString("KAFKA_FRAMEWORK", "kafka"),
		"The Kafka framework to query for brokers. (overrides '-brokers')")
)

func newAsyncProducer(brokerList []string) (producer sarama.AsyncProducer, err error) {
	// For the access log, we are looking for AP semantics, with high throughput.
	// By creating batches of compressed messages, we reduce network I/O at a cost of more latency.
	config := sarama.NewConfig()
	config.Producer.RequiredAcks = sarama.WaitForLocal       // Only wait for the leader to ack
	config.Producer.Compression = sarama.CompressionSnappy   // Compress messages
	config.Producer.Flush.Frequency = 500 * time.Millisecond // Flush batches every 500ms

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

func GetKafkaProducer() (kafkaProducer sarama.AsyncProducer, err error) {
	brokers := make([]string, 0)
	if *frameworkFlag != "" {
		foundBrokers, err := LookupBrokers(*frameworkFlag)
		brokers = append(brokers, foundBrokers...)
		if err != nil {
			return nil, errors.New(fmt.Sprintf(
				"Broker lookup against framework %s failed: %s", *frameworkFlag, err))
		}
	} else if *brokersFlag != "" {
		brokers = strings.Split(*brokersFlag, ",")
		if len(brokers) == 0 {
			log.Fatal("-brokers must be non-empty.")
		}
	} else {
		flag.Usage()
		log.Fatal("Either -framework or -brokers must be specified, or -kafka must be false.")
	}
	log.Printf("Kafka brokers: %s", strings.Join(brokers, ", "))

	kafkaProducer, err = newAsyncProducer(brokers)
	if err != nil {
		return nil, errors.New(fmt.Sprintf(
			"Producer creation against brokers %+v failed: %s", brokers, err))
	}
	return kafkaProducer, nil
}
