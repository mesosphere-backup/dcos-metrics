package collector

import (
	"bytes"
	"encoding/hex"
	"errors"
	"fmt"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"log"
	"time"
)

var (
	kafkaProduceCountFlag = IntEnvFlag("kafka-produce-count", 1024,
		"The number of Avro records to accumulate in a Kafka record before passing to the Kafka Producer.")
	kafkaProduceKBytesFlag = IntEnvFlag("kafka-produce-kbytes", 512,
		"The approximate number of KB to accumulate in a single Kafka record before passing to the Kafka Producer. Should be well under 1024KB.")
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
	agentIdTag         = "agent_id"
	frameworkIdTag     = "framework_id"
	frameworkNameTag   = "framework_name"
	executorIdTag      = "executor_id"
	applicationNameTag = "application_name"
)

// A single Avro record with some metadata about it
type AvroDatum struct {
	// the goavro.Record itself
	rec interface{}
	// the topic that the Record requested
	topic string
	// the approximate byte size of the original Record, if known
	approxBytes int64
}

// A collection of Avro records along with their approximate total byte size
type avroData struct {
	// goavro.Records
	recs []interface{}
	// the approximate sum byte size of the records
	approxBytes int64
}

func newAvroData() avroData {
	return avroData{make([]interface{}, 0), 0}
}
func (d *avroData) append(datum *AvroDatum) {
	d.recs = append(d.recs, datum.rec)
	d.approxBytes += datum.approxBytes
}

// Sorts incoming Avro records into Kafka topics
func RunTopicSorter(avroInput <-chan *AvroDatum, agentStateInput <-chan *AgentState, kafkaOutput chan<- KafkaMessage, stats chan<- StatsEvent) {
	codec, err := goavro.NewCodec(metrics_schema.MetricListSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}

	topics := make(map[string]avroData)
	produceTicker := time.NewTicker(time.Millisecond * time.Duration(*kafkaProducePeriodMsFlag))
	resetLimitTicker := time.NewTicker(time.Second * time.Duration(*globalLimitPeriodFlag))
	var agentState *AgentState = nil
	var totalRecordCount int64
	var totalByteCount int64
	var droppedByteCount int64
	for {
		select {
		case avroDatum := <-avroInput:
			// sort into correct topic (and flush if topic has reached size limit)
			var topic string
			if len(*kafkaSingleTopicFlag) != 0 {
				topic = *kafkaSingleTopicFlag
			} else {
				topic = *kafkaTopicPrefixFlag + avroDatum.topic
			}
			topicData, ok := topics[topic]
			if !ok {
				topicData = newAvroData()
			}
			topicData.append(avroDatum)
			var flushReason string
			if len(topicData.recs) >= int(*kafkaProduceCountFlag) {
				// topic has hit record limit, flush now
				flushReason = fmt.Sprintf("%d recs", *kafkaProduceCountFlag)
			} else if topicData.approxBytes >= 1024**kafkaProduceKBytesFlag {
				// topic has hit byte limit, flush now
				flushReason = fmt.Sprintf("%d KB", *kafkaProduceKBytesFlag)
			} else {
				flushReason = ""
			}
			if len(flushReason) != 0 {
				// topic has hit a flush threshould, flush now
				processRecs(agentState, topicData.recs, stats)
				flushTopic(topic, topicData.recs, codec,
					flushReason,
					kafkaOutput, stats,
					&totalRecordCount, &totalByteCount, &droppedByteCount)
				// wipe this map entry after it's been flushed
				delete(topics, topic)
			} else {
				// ensure map is up to date
				topics[topic] = topicData
			}
		case state := <-agentStateInput:
			// got updated agent state, use for future record flushes
			agentState = state
			log.Printf("Agent state updated: id=%s, frameworks(%d), applications(%d)",
				agentState.agentId, len(agentState.frameworkNames), len(agentState.executorAppNames))
		case _ = <-produceTicker.C:
			// timeout reached: flush any pending data
			flushReason := fmt.Sprintf("%d ms", *kafkaProducePeriodMsFlag)
			if len(topics) == 0 {
				log.Printf("No Kafka topics to flush after %s\n", flushReason)
			}
			for topic, topicData := range topics {
				processRecs(agentState, topicData.recs, stats)
				flushTopic(topic, topicData.recs, codec,
					flushReason,
					kafkaOutput, stats,
					&totalRecordCount, &totalByteCount, &droppedByteCount)
			}
			// wipe the whole map after all entries are flushed
			topics = make(map[string]avroData)
		case _ = <-resetLimitTicker.C:
			// timeout reached: reset output counter (for global throttling)
			if droppedByteCount != 0 {
				log.Printf("OUTPUT SUMMARY: Processed %d MetricLists (%d KB) for sending in the last %ds. Of this, %d KB was dropped due to throttling.\n",
					totalRecordCount, totalByteCount/1024, *globalLimitPeriodFlag, droppedByteCount/1024)
			} else {
				log.Printf("OUTPUT SUMMARY: Processed %d MetricLists (%d KB) for sending in the last %ds\n",
					totalRecordCount, totalByteCount/1024, *globalLimitPeriodFlag)
			}
			totalRecordCount = 0
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
	totalRecordCount, totalByteCount, droppedByteCount *int64) {
	stats <- MakeEventSuffCount(AvroRecordOut, topic, len(topicRecs))
	*totalRecordCount += int64(len(topicRecs))

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

		// Append to tags:
		// - matching framework_name for the included framework_id
		// - matching application_name for the included marathon info (if applicable)
		// (or error values if applicable)

		if agentState == nil {
			// haven't gotten agent state yet (skip framework_name; it's irrelevant to many stats)
			tags = addTag(tags, agentIdTag, "UNKNOWN_AGENT_STATE")
		} else {
			frameworkName := findFrameworkName(tags, agentState, stats)
			if len(frameworkName) != 0 {
				tags = addTag(tags, frameworkNameTag, frameworkName)
			}
			applicationName := findApplicationName(tags, agentState, stats)
			if len(applicationName) != 0 {
				tags = addTag(tags, applicationNameTag, applicationName)
			}
			tags = addTag(tags, agentIdTag, agentState.agentId)
		}

		// Update tags in record
		record.Set("tags", tags)
	}
}

func findTagValue(tags []interface{}, key string, stats chan<- StatsEvent) (string, error) {
	value := ""
	for _, tagObj := range tags {
		tag, ok := tagObj.(*goavro.Record)
		if !ok {
			stats <- MakeEvent(RecordBadTags)
			return "", errors.New("Unable to convert tags object to avro Record")
		}
		tagKey, err := tag.Get("key")
		if err != nil {
			stats <- MakeEvent(RecordBadTags)
			return "", errors.New("Unable to get key object")
		}
		if tagKey == key {
			valueObj, err := tag.Get("value")
			if err != nil {
				stats <- MakeEvent(RecordBadTags)
				return "", errors.New("Unable to get value object")
			}
			value, ok = valueObj.(string)
			if !ok {
				stats <- MakeEvent(RecordBadTags)
				return "", errors.New("Unable to convert value object to string")
			}
			break
		}
	}
	return value, nil
}

func findFrameworkName(tags []interface{}, agentState *AgentState, stats chan<- StatsEvent) string {
	frameworkId, err := findTagValue(tags, frameworkIdTag, stats)
	if err != nil {
		// Failed to access tags at all
		return "ERROR_BAD_RECORD"
	}
	if len(frameworkId) == 0 {
		// Data lacks a framework id. This means the data isn't tied to a specific framework.
		// Don't include a "framework_name" tag.
		return ""
	}
	frameworkName, ok := agentState.frameworkNames[frameworkId]
	if ok {
		return frameworkName
	}
	// didn't find this framework id in agent state. this is expected to always be present,
	// so a missing value implies some kind of problems with state.json.
	return "UNKNOWN_FRAMEWORK_ID"
}

func findApplicationName(tags []interface{}, agentState *AgentState, stats chan<- StatsEvent) string {
	executorId, err := findTagValue(tags, executorIdTag, stats)
	if err != nil {
		// Failed to access tags at all
		return "ERROR_BAD_RECORD"
	}
	if len(executorId) == 0 {
		// Data lacks an executor id. This means the data isn't tied to a specific framework.
		// Don't include an "application_name" tag.
		return ""
	}
	applicationName, ok := agentState.executorAppNames[executorId]
	if ok {
		return applicationName
	}
	// didn't find this executor id in agent state. unlike with framework_id, a missing executor_id
	// is expected for marathon, so just exclude the tag when this happens.
	return ""
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
