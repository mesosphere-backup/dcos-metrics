package main

import (
	"bytes"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"log"
	"time"
)

var (
	kafkaProduceCountFlag = collector.IntEnvFlag("kafka-produce-count", 1024,
		"The number of Avro records to accumulate in a Kafka record before passing to the Kafka Producer.")
	kafkaProducePeriodFlag = collector.IntEnvFlag("kafka-produce-period-ms", 15000,
		"Interval period between calls to the Kafka Producer.")
	kafkaTopicPrefixFlag = collector.StringEnvFlag("kafka-topic-prefix", "metrics-",
		"Prefix string to include on Kafka topic labels (ignored if -kafka-single-topic is used)")
	kafkaSingleTopicFlag = collector.StringEnvFlag("kafka-single-topic", "",
		"If non-empty, writes all metric data to a single Kafka topic, ignoring record.topic values")
	logRecordOutputFlag = collector.BoolEnvFlag("log-record-output", false,
		"Whether to log the parsed content of outgoing records")
)

type sortedRecs []interface{}

// Sorts incoming Avro records into Kafka topics
func RunTopicSorter(
	avroInput <-chan interface{}, kafkaOutput chan<- collector.KafkaMessage, stats chan<- collector.StatsEvent) {
	codec, err := goavro.NewCodec(metrics_schema.MetricListSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}

	topics := make(map[string][]interface{})
	ticker := time.NewTicker(time.Millisecond * time.Duration(*kafkaProducePeriodFlag))
	buf := new(bytes.Buffer)
	for {
		select {
		case record := <-avroInput:
			topic := getTopic(record)
			topicRecs := append(topics[topic], record)
			if len(topicRecs) >= int(*kafkaProduceCountFlag) {
				// topic has hit size limit, flush (and wipe) preemptively
				log.Printf("Producing full topic (%d records) to Kafka: '%s'\n", len(topicRecs), topic)
				stats <- collector.MakeEventSuffCount(collector.AvroRecordOut, topic, len(topicRecs))

				err = serializeRecs(buf, topicRecs, codec)
				if err != nil {
					log.Printf("Failed to serialize %d records: %s\n", len(topicRecs), err)
					stats <- collector.MakeEvent(collector.AvroWriterFailed)
				} else {
					flushTopic(kafkaOutput, topic, buf)
				}
				delete(topics, topic)
			} else {
				topics[topic] = topicRecs
			}
		case _ = <-ticker.C:
			// timeout reached, flush all data
			log.Printf("Producing %d topics to Kafka", len(topics))
			for topic, topicRecs := range topics {
				log.Printf("- Topic '%s': %d records\n", topic, len(topicRecs))
				stats <- collector.MakeEventSuffCount(collector.AvroRecordOut, topic, len(topicRecs))

				err = serializeRecs(buf, topicRecs, codec)
				if err != nil {
					log.Printf("Failed to serialize %d records: %s\n", len(topicRecs), err)
					stats <- collector.MakeEvent(collector.AvroWriterFailed)
				} else {
					flushTopic(kafkaOutput, topic, buf)
				}
			}
			topics = make(map[string][]interface{})
		}
	}
}

// ---

func getTopic(obj interface{}) string {
	if len(*kafkaSingleTopicFlag) != 0 {
		return *kafkaSingleTopicFlag
	}

	record, ok := obj.(*goavro.Record)
	if !ok {
		return *kafkaTopicPrefixFlag + "UNKNOWN_TYPE"
	}
	topicObj, err := record.Get("topic")
	if err != nil {
		return *kafkaTopicPrefixFlag + "UNKNOWN_VAL"
	}
	topicStr, ok := topicObj.(string)
	return *kafkaTopicPrefixFlag + topicStr
}

func serializeRecs(buf *bytes.Buffer, recs []interface{}, codec goavro.Codec) error {
	// Recreate OCF writer for each chunk, so that it writes a header at the top each time:
	avroWriter, err := goavro.NewWriter(
		goavro.BlockSize(int64(len(recs))), goavro.ToWriter(buf), goavro.UseCodec(codec))
	if err != nil {
		return err
	}
	for _, rec := range recs {
		if *logRecordOutputFlag {
			log.Println("RECORD OUT:", rec)
		}
		avroWriter.Write(rec)
	}

	return avroWriter.Close() // ensure flush to buf occurs before buf is used
}

func flushTopic(kafkaOutput chan<- collector.KafkaMessage, topic string, buf *bytes.Buffer) {
	kafkaOutput <- collector.KafkaMessage{
		Topic: topic,
		Data:  buf.Bytes(),
	}
	buf.Reset()
}
