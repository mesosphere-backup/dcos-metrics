package metric

import (
	"testing"

	"github.com/dcos/dcos-metrics/consumer/config"
)

func TestNewKafkaMetric(t *testing.T) {
	testConf := &config.ConsumerConfig{}
	testConf.Kafka.Brokers = "foo:123,bar:123"
	testConf.Kafka.Topic = "test_topic"
	testConf.Kafka.Partition = 1
	testConf.Kafka.Offset = 2

	km, err := NewKafkaMetric(testConf)
	if err != nil {
		t.Errorf("Expected no errors from NewKafkaMetric(), got %s", err)
	}

	if km.Brokers[0] != "foo:123" || km.Brokers[1] != "bar:123" {
		t.Error("Expected []string{'foo:123', 'bar:123'}, got", km.Brokers)
	}

	if km.Topic != "test_topic" {
		t.Error("Expected test_topic, got", km.Topic)
	}

	if km.Partition != 1 {
		t.Error("Expected 1, got", km.Partition)
	}

	if km.Offset != 2 {
		t.Error("expected 2 offset, got", km.Offset)
	}
}
