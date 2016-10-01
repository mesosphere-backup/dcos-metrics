package metric

import (
	"errors"
	"strings"

	log "github.com/Sirupsen/logrus"

	kafka "github.com/Shopify/sarama"
	"github.com/dcos/dcos-metrics/consumer/config"
)

type KafkaMetric struct {
	Metric
	Brokers   []string
	Topic     string
	Partition int32
	Offset    int64
}

func (k *KafkaMetric) GetMessageChan() chan Message {
	return k.Metric.IO
}

func (k *KafkaMetric) SetupMessageChan() error {
	log.Infof("Attempting to connect to Kafka brokers: %s", k.Brokers)
	kafkaConfig := kafka.NewConfig()
	kafkaConsumer, err := kafka.NewConsumer(k.Brokers, kafkaConfig)
	if err != nil {
		return err
	}

	consumeTopic, err := kafkaConsumer.ConsumePartition(k.Topic, k.Partition, k.Offset)
	if err != nil {
		return err
	}

	k.Metric.IO = make(chan Message)
	go func() {
		defer kafkaConsumer.Close()
		for msg := range consumeTopic.Messages() {
			log.Debugf("Kafka Message Found @%s", msg.Timestamp)
			k.Metric.IO <- Message{
				Key:       string(msg.Key),
				Value:     string(msg.Value),
				Topic:     msg.Topic,
				Timestamp: msg.Timestamp,
			}
		}
	}()

	return nil
}

func NewKafkaMetric(conf *config.ConsumerConfig) (*KafkaMetric, error) {
	if conf.Kafka.Offset == 0 {
		log.Warn("Setting offset to newest as it appears unset (0)")
		conf.Kafka.Offset = kafka.OffsetNewest
	}

	if conf.Kafka.Topic == "" {
		return &KafkaMetric{}, errors.New("Must pass `--topic` when querying Kafka")
	}

	return &KafkaMetric{
		Brokers:   strings.Split(conf.Kafka.Brokers, ","),
		Topic:     conf.Kafka.Topic,
		Partition: int32(conf.Kafka.Partition),
		Offset:    conf.Kafka.Offset,
	}, nil
}
