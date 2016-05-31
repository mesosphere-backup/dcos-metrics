package collector

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"log"
	"time"
)

var (
	kafkaProduceCountFlag = IntEnvFlag("kafka-produce-count", 1024,
		"The number of Avro records to accumulate in a Kafka record before passing to the Kafka Producer.")
	kafkaProducePeriodMsFlag = IntEnvFlag("kafka-produce-period-ms", 15000,
		"Interval period between calls to the Kafka Producer.")
	kafkaTopicPrefixFlag = StringEnvFlag("kafka-topic-prefix", "metrics-",
		"Prefix string to include on Kafka topic labels (ignored if -kafka-single-topic is used)")
	kafkaSingleTopicFlag = StringEnvFlag("kafka-single-topic", "",
		"If provided, writes all metric data to a single Kafka topic, ignoring record.topic values")
	recordOutputLogFlag = BoolEnvFlag("record-output-log", false,
		"Logs the parsed content of outgoing records")
	recordOutputHexdumpFlag = BoolEnvFlag("record-output-hexdump", false,
		"Prints a verbose hex dump of outgoing records (ala 'hexdump -C')")
)

const (
	AgentIdTag       = "agent_id"
	FrameworkIdTag   = "framework_id"
	FrameworkNameTag = "framework_name"
)

type sortedRecs []interface{}

// Sorts incoming Avro records into Kafka topics
func RunTopicSorter(avroInput <-chan interface{}, agentStateInput <-chan *AgentState, kafkaOutput chan<- KafkaMessage, stats chan<- StatsEvent) {
	codec, err := goavro.NewCodec(metrics_schema.MetricListSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}

	topics := make(map[string][]interface{})
	ticker := time.NewTicker(time.Millisecond * time.Duration(*kafkaProducePeriodMsFlag))
	var agentState *AgentState = nil
	for {
		select {
		case record := <-avroInput:
			// got record, sort into correct topic (and flush if topic has reached size limit)
			topic := getTopic(record, stats)
			topicRecs := append(topics[topic], record)
			if len(topicRecs) >= int(*kafkaProduceCountFlag) {
				// topic has hit size limit, flush now
				processRecs(agentState, topicRecs, stats)
				flushTopic(topic, topicRecs, codec, fmt.Sprintf("%dms", *kafkaProduceCountFlag), kafkaOutput, stats)
				// wipe this map entry after it's been flushed
				delete(topics, topic)
			} else {
				topics[topic] = topicRecs
			}
		case state := <-agentStateInput:
			// got updated agent state, use for future record flushes
			agentState = state
			log.Printf("Agent state updated: id=%s, frameworks(%d):", agentState.agentId, len(agentState.frameworkNames))
			for id, name := range agentState.frameworkNames {
				log.Printf("- %s = %s\n", id, name)
			}
		case _ = <-ticker.C:
			// timeout reached, flush all pending data
			flushReason := fmt.Sprintf("%dms", *kafkaProducePeriodMsFlag)
			if len(topics) == 0 {
				log.Printf("No Kafka topics to flush after %s\n", flushReason)
			}
			for topic, topicRecs := range topics {
				processRecs(agentState, topicRecs, stats)
				flushTopic(topic, topicRecs, codec, flushReason, kafkaOutput, stats)
			}
			// wipe the whole map after all entries are flushed
			topics = make(map[string][]interface{})
		}
	}
}

// ---

func getTopic(obj interface{}, stats chan<- StatsEvent) string {
	if len(*kafkaSingleTopicFlag) != 0 {
		return *kafkaSingleTopicFlag
	}

	record, ok := obj.(*goavro.Record)
	if !ok {
		stats <- MakeEvent(RecordBadTopic)
		return *kafkaTopicPrefixFlag + "UNKNOWN_RECORD_TYPE"
	}
	topicObj, err := record.Get("topic")
	if err != nil {
		stats <- MakeEvent(RecordBadTopic)
		return *kafkaTopicPrefixFlag + "UNKNOWN_TOPIC_VAL"
	}
	topicStr, ok := topicObj.(string)
	if !ok {
		stats <- MakeEvent(RecordBadTopic)
		return *kafkaTopicPrefixFlag + "UNKNOWN_TOPIC_TYPE"
	}
	return *kafkaTopicPrefixFlag + topicStr
}

func flushTopic(topic string, topicRecs []interface{}, codec goavro.Codec, logReason string, kafkaOutput chan<- KafkaMessage, stats chan<- StatsEvent) {
	stats <- MakeEventSuffCount(AvroRecordOut, topic, len(topicRecs))

	buf, err := serializeRecs(topicRecs, codec)
	if err != nil {
		log.Printf("Failed to serialize %d records for Kafka topic %s: %s\n",
			len(topicRecs), topic, err)
		stats <- MakeEvent(AvroWriterFailed)
		return
	}
	log.Printf("Producing %d MetricLists (%d bytes) for Kafka topic '%s' (trigger: %s)\n",
		len(topicRecs), buf.Len(), topic, logReason)
	kafkaOutput <- KafkaMessage{
		Topic: topic,
		Data:  buf.Bytes(),
	}
}

// Adds additional agent info to the provided records
func processRecs(agentState *AgentState, recs []interface{}, stats chan<- StatsEvent) {
	for _, rec := range recs {
		// Fetch current tags
		record, ok := rec.(*goavro.Record)
		if !ok {
			stats <- MakeEvent(RecordBadTags)
			continue
		}
		tagsObj, err := record.Get("tags")
		if err != nil {
			stats <- MakeEvent(RecordBadTags)
			continue
		}
		tags, ok := tagsObj.([]interface{})
		if !ok {
			stats <- MakeEvent(RecordBadTags)
			continue
		}

		// Update tags to contain matching framework_name for the included framework_id
		// (or a stub value if framework_id is missing or unknown)

		if agentState == nil {
			// haven't gotten agent state yet (skip framework_name; it's irrelevant to many stats)
			tags = addTag(tags, AgentIdTag, "UNKNOWN_AGENT_STATE")
		} else {
			frameworkName := findFrameworkName(tags, agentState, stats)
			if len(frameworkName) != 0 {
				tags = addTag(tags, FrameworkNameTag, frameworkName)
			}
			tags = addTag(tags, AgentIdTag, agentState.agentId)
		}

		// Update tags in record
		record.Set("tags", tags)
	}
}

func findFrameworkName(tags []interface{}, agentState *AgentState, stats chan<- StatsEvent) string {
	frameworkId := ""
	for _, tagObj := range tags {
		tag, ok := tagObj.(*goavro.Record)
		if !ok {
			stats <- MakeEvent(RecordBadTags)
			return "ERROR_BAD_RECORD"
		}
		tagKey, err := tag.Get("key")
		if err != nil {
			stats <- MakeEvent(RecordBadTags)
			return "ERROR_BAD_RECORD"
		}
		if tagKey == FrameworkIdTag {
			frameworkIdObj, err := tag.Get("value")
			if err != nil {
				stats <- MakeEvent(RecordBadTags)
				return "ERROR_BAD_RECORD"
			}
			frameworkId, ok = frameworkIdObj.(string)
			if !ok {
				stats <- MakeEvent(RecordBadTags)
				return "ERROR_BAD_RECORD"
			}
			break
		}
	}
	if len(frameworkId) == 0 {
		// Data lacks a framework id. This means the data isn't tied to a specific framework.
		// Don't include a "framework_name" tag.
		return ""
	}
	for stateFrameworkId, stateFrameworkName := range agentState.frameworkNames {
		if stateFrameworkId == frameworkId {
			return stateFrameworkName
		}
	}
	// didn't find this framework id in agent state
	return "UNKNOWN_FRAMEWORK_ID"
}

func addTag(tags []interface{}, key string, value string) []interface{} {
	tag, err := goavro.NewRecord(tagNamespace, tagSchema)
	if err != nil {
		log.Fatal("Failed to create Tag record: ", err)
	}
	tag.Set("key", key)
	tag.Set("value", value)
	return append(tags, tag)
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
		if *recordOutputLogFlag {
			log.Println("RECORD OUT:", rec)
		}
		avroWriter.Write(rec)
	}

	err = avroWriter.Close() // ensure flush to buf occurs before buf is used
	if err == nil && *recordOutputHexdumpFlag {
		log.Printf("Hex dump of %d records (%d bytes):\n%sEnd dump of %d records (%d bytes)",
			len(recs), buf.Len(), hex.Dump(buf.Bytes()), len(recs), buf.Len())
	}
	return buf, err
}
