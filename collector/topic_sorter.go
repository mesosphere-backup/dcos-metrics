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

	globalLimitAmountKBytesFlag = IntEnvFlag("global-limit-amount-kbytes", 40960,
		"The amount of data that will be accepted from all inputs in -input-limit-period. "+
			"Records from all inputs beyond this limit will be dropped until the period resets. "+
			"This value is applied on a COLLECTOR-WIDE basis.")
	globalLimitPeriodFlag = IntEnvFlag("global-limit-period", 60,
		"Number of seconds over which to enforce -input-limit-amount-kbytes")

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

type AvroData struct {
	// the goavro.Record itself
	rec interface{}
	// the topic that the Record requested
	topic string
}

// Sorts incoming Avro records into Kafka topics
func RunTopicSorter(avroInput <-chan *AvroData, agentStateInput <-chan *AgentState, kafkaOutput chan<- KafkaMessage, stats chan<- StatsEvent) {
	codec, err := goavro.NewCodec(metrics_schema.MetricListSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}

	topics := make(map[string][]interface{})
	produceTicker := time.NewTicker(time.Millisecond * time.Duration(*kafkaProducePeriodMsFlag))
	resetLimitTicker := time.NewTicker(time.Second * time.Duration(*globalLimitPeriodFlag))
	var agentState *AgentState = nil
	var totalByteCount int64
	var droppedByteCount int64
	for {
		select {
		case avroData := <-avroInput:
			// sort into correct topic (and flush if topic has reached size limit)
			var topic string
			if len(*kafkaSingleTopicFlag) != 0 {
				topic = *kafkaSingleTopicFlag
			} else {
				topic = *kafkaTopicPrefixFlag + avroData.topic
			}
			topicRecs := append(topics[topic], avroData.rec)
			if len(topicRecs) >= int(*kafkaProduceCountFlag) {
				// topic has hit size limit, flush now
				processRecs(agentState, topicRecs, stats)
				flushTopic(topic, topicRecs, codec,
					fmt.Sprintf("%d recs", *kafkaProduceCountFlag),
					kafkaOutput, stats,
					&totalByteCount, &droppedByteCount)
				// wipe this map entry after it's been flushed
				delete(topics, topic)
			} else {
				topics[topic] = topicRecs
			}
		case state := <-agentStateInput:
			// got updated agent state, use for future record flushes
			agentState = state
			log.Printf("Agent state updated: id=%s, frameworks(%d):",
				agentState.agentId, len(agentState.frameworkNames))
			for id, name := range agentState.frameworkNames {
				log.Printf("- %s = %s\n", id, name)
			}
		case _ = <-produceTicker.C:
			// timeout reached: flush any pending data
			flushReason := fmt.Sprintf("%d ms", *kafkaProducePeriodMsFlag)
			if len(topics) == 0 {
				log.Printf("No Kafka topics to flush after %s\n", flushReason)
			}
			for topic, topicRecs := range topics {
				processRecs(agentState, topicRecs, stats)
				flushTopic(topic, topicRecs, codec,
					flushReason,
					kafkaOutput, stats,
					&totalByteCount, &droppedByteCount)
			}
			// wipe the whole map after all entries are flushed
			topics = make(map[string][]interface{})
		case _ = <-resetLimitTicker.C:
			// timeout reached: reset output counter (for global throttling)
			if droppedByteCount != 0 {
				log.Printf("SUMMARY: Processed %d KB for sending in the last %ds. Of this, %d KB was dropped due to throttling.\n",
					totalByteCount/1024, *globalLimitPeriodFlag, droppedByteCount/1024)
			} else {
				log.Printf("SUMMARY: Processed %d KB for sending in the last %ds\n",
					totalByteCount, *globalLimitPeriodFlag)
			}
			totalByteCount = 0
			droppedByteCount = 0
		}
	}
}

// Extracts the topic value from the provided Avro record object, or a stub value with "false" if the topic wasn't retrievable.
func GetTopic(obj interface{}) (string, bool) {
	record, ok := obj.(*goavro.Record)
	if !ok {
		return "UNKNOWN_RECORD_TYPE", false
	}
	topicObj, err := record.Get("topic")
	if err != nil {
		return "UNKNOWN_TOPIC_VAL", false
	}
	topicStr, ok := topicObj.(string)
	if !ok {
		return "UNKNOWN_TOPIC_TYPE", false
	}
	return topicStr, true
}

// ---

func flushTopic(topic string, topicRecs []interface{}, codec goavro.Codec,
	logReason string,
	kafkaOutput chan<- KafkaMessage, stats chan<- StatsEvent,
	totalByteCount *int64, droppedByteCount *int64) {
	stats <- MakeEventSuffCount(AvroRecordOut, topic, len(topicRecs))

	buf, err := serializeRecs(topicRecs, codec)
	if err != nil {
		log.Printf("Failed to serialize %d records for Kafka topic %s: %s\n",
			len(topicRecs), topic, err)
		stats <- MakeEvent(AvroWriterFailed)
		return
	}
	stats <- MakeEventSuffCount(AvroBytesOut, topic, buf.Len())

	// enforce AFTER add: always let some data get through
	if *totalByteCount > *globalLimitAmountKBytesFlag*1024 {
		log.Printf("Dropping %d MetricLists (%d bytes) for Kafka topic '%s' (trigger: %s)\n",
			len(topicRecs), buf.Len(), topic, logReason)
		stats <- MakeEventSuffCount(AvroRecordOutThrottled, topic, len(topicRecs))
		stats <- MakeEventSuffCount(AvroBytesOutThrottled, topic, buf.Len())
		*droppedByteCount += int64(buf.Len())
	} else {
		log.Printf("Producing %d MetricLists (%d bytes) for Kafka topic '%s' (trigger: %s)\n",
			len(topicRecs), buf.Len(), topic, logReason)
		kafkaOutput <- KafkaMessage{
			Topic: topic,
			Data:  buf.Bytes(),
		}
	}
	*totalByteCount += int64(buf.Len())
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
		log.Printf("Hex dump of %d output records (%d bytes):\n%sEnd dump of %d output records (%d bytes)",
			len(recs), buf.Len(), hex.Dump(buf.Bytes()), len(recs), buf.Len())
	}
	return buf, err
}
