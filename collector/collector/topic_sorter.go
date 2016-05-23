package main

import (
	"bytes"
	"encoding/hex"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"log"
	"time"
)

var (
	kafkaProduceCountFlag = collector.IntEnvFlag("kafka-produce-count", 1024,
		"The number of Avro records to accumulate in a Kafka record before passing to the Kafka Producer.")
	kafkaProducePeriodMsFlag = collector.IntEnvFlag("kafka-produce-period-ms", 15000,
		"Interval period between calls to the Kafka Producer.")
	kafkaTopicPrefixFlag = collector.StringEnvFlag("kafka-topic-prefix", "metrics-",
		"Prefix string to include on Kafka topic labels (ignored if -kafka-single-topic is used)")
	kafkaSingleTopicFlag = collector.StringEnvFlag("kafka-single-topic", "",
		"If non-empty, writes all metric data to a single Kafka topic, ignoring record.topic values")
	logRecordOutputFlag = collector.BoolEnvFlag("log-record-output", false,
		"Whether to log the parsed content of outgoing records")
	hexDumpOutputFlag = collector.BoolEnvFlag("hexdump-record-output", false,
		"Whether to print a verbose hex dump of outgoing records")
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
	ticker := time.NewTicker(time.Millisecond * time.Duration(*kafkaProducePeriodMsFlag))
	for {
		select {
		case record := <-avroInput:
			topic := getTopic(record)
			topicRecs := append(topics[topic], record)
			if len(topicRecs) >= int(*kafkaProduceCountFlag) {
				// topic has hit size limit, flush (and wipe) preemptively
				stats <- collector.MakeEventSuffCount(collector.AvroRecordOut, topic, len(topicRecs))

				buf, err := serializeRecs(topicRecs, codec)
				if err != nil {
					log.Printf("Failed to serialize %d records for Kafka topic %s: %s\n",
						len(topicRecs), topic, err)
					stats <- collector.MakeEvent(collector.AvroWriterFailed)
				} else {
					log.Printf("Producing %d MetricLists (%d bytes) for Kafka topic '%s' (trigger: %d records)\n",
						len(topicRecs), buf.Len(), topic, *kafkaProduceCountFlag)
					kafkaOutput <- collector.KafkaMessage{
						Topic: topic,
						Data:  buf.Bytes(),
					}
				}
				delete(topics, topic)
			} else {
				topics[topic] = topicRecs
			}
		case _ = <-ticker.C:
			// timeout reached, flush all pending data
			if len(topics) == 0 {
				log.Printf("No Kafka topics to flush after %dms\n", *kafkaProducePeriodMsFlag)
			}
			for topic, topicRecs := range topics {
				stats <- collector.MakeEventSuffCount(collector.AvroRecordOut, topic, len(topicRecs))

				buf, err := serializeRecs(topicRecs, codec)
				if err != nil {
					log.Printf("Failed to serialize %d records for Kafka topic %s: %s\n",
						len(topicRecs), topic, err)
					stats <- collector.MakeEvent(collector.AvroWriterFailed)
				} else {
					log.Printf("Producing %d MetricLists (%d bytes) for Kafka topic '%s' (trigger: %dms)\n",
						len(topicRecs), buf.Len(), topic, *kafkaProducePeriodMsFlag)
					kafkaOutput <- collector.KafkaMessage{
						Topic: topic,
						Data:  buf.Bytes(),
					}
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

// Serializes the provided Avro records into a newly created buffer.
// NOTE: DO NOT reuse buffers after sending them to the Kafka output channel, or else they will
// be corrupted in-flight!
func serializeRecs(recs []interface{}, codec goavro.Codec) (*bytes.Buffer, error) {
	// Recreate OCF writer for each chunk, so that it writes a header at the top each time:
	buf := new(bytes.Buffer)
	avroWriter, err := goavro.NewWriter(
		goavro.BlockSize(int64(len(recs))), goavro.ToWriter(buf), goavro.UseCodec(codec))
	if err != nil {
		return nil, err
	}
	for _, rec := range recs {
		if *logRecordOutputFlag {
			log.Println("RECORD OUT:", rec)
		}
		avroWriter.Write(rec)
	}

	err = avroWriter.Close() // ensure flush to buf occurs before buf is used
	if err == nil && *hexDumpOutputFlag {
		log.Printf("Hex dump of %d records (%d bytes):\n%sEnd dump of %d records (%d bytes)",
			len(recs), buf.Len(), hex.Dump(buf.Bytes()), len(recs), buf.Len())
	}
	return buf, err
}
