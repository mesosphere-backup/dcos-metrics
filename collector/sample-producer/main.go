package main

import (
	"bytes"
	"flag"
	"fmt"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"log"
	"os"
	"time"
)

var (
	kafkaOutputFlag = collector.BoolEnvFlag("kafka-enabled", true,
		"Enable sending data to Kafka.")
	fileOutputFlag = collector.BoolEnvFlag("file-enabled", false,
		"Write chunk-<N>.avro files containing the collected data.")
	topicFlag = collector.StringEnvFlag("kafka-topic", "sample_metrics",
		"The Kafka topic to write data against.")
	pollPeriodFlag = collector.IntEnvFlag("period", 15,
		"Seconds to wait between stats refreshes")
)

func sleep() {
	log.Printf("Wait for %ds...\n", *pollPeriodFlag)
	time.Sleep(time.Duration(*pollPeriodFlag) * time.Second)
}

func main() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr,
			"Sends various stats in Metrics Avro format to the provided Kafka service\n")
		fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()

	stats := make(chan collector.StatsEvent)
	go collector.RunStatsEmitter(stats)

	kafkaOutputChan := make(chan collector.KafkaMessage)
	if *kafkaOutputFlag {
		go collector.RunKafkaProducer(kafkaOutputChan, stats)
	}

	// Wrap buf in an AvroWriter
	codec, err := goavro.NewCodec(metrics_schema.MetricListSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}

	agentIp, err := AgentGetIp()
	if err != nil {
		stats <- collector.MakeEvent(collector.AgentIPFailed)
		log.Fatal("Failed to get agent IP: ", err)
	}

	buf := new(bytes.Buffer)
	chunkid := 0
	for {
		// Get stats from agent
		recs, err := AgentStatisticsAvro(agentIp, *topicFlag)
		if err != nil {
			stats <- collector.MakeEvent(collector.AgentQueryFailed)
			fmt.Fprintf(os.Stderr,
				"Failed to fetch/parse stats from agent at %s: %s\n", agentIp, err)
			// sleep and retry
			sleep()
			continue
		} else if len(recs) == 0 {
			stats <- collector.MakeEvent(collector.AgentQueryEmpty)
			log.Printf("No containers returned in stats, trying again in %ds\n", *pollPeriodFlag)
			// sleep and retry
			sleep()
			continue
		}
		stats <- collector.MakeEvent(collector.AgentQueried)

		// Recreate OCF writer for each chunk, so that it writes a header at the top each time:
		avroWriter, err := goavro.NewWriter(
			goavro.BlockSize(int64(len(recs))), goavro.ToWriter(buf), goavro.UseCodec(codec))
		if err != nil {
			stats <- collector.MakeEvent(collector.AvroWriterFailed)
			log.Fatal("Failed to create avro writer: ", err)
		}
		for _, rec := range recs {
			stats <- collector.MakeEventSuff(collector.AvroRecordOut, *topicFlag)
			avroWriter.Write(rec)
		}

		err = avroWriter.Close() // ensure flush to buf occurs before buf is used
		if err != nil {
			stats <- collector.MakeEvent(collector.AvroWriterFailed)
			log.Fatal("Couldn't flush output: ", err)
		}
		if *kafkaOutputFlag {
			log.Printf("Sending avro-formatted stats for %d containers to topic '%s'.",
				len(recs), *topicFlag)
			kafkaOutputChan <- collector.KafkaMessage{
				Topic: *topicFlag,
				Data:  buf.Bytes(),
			}
		}
		if *fileOutputFlag {
			filename := fmt.Sprintf("chunk-%d.avro", chunkid)
			chunkid++
			log.Printf("Writing avro-formatted stats for %d containers to file '%s'.",
				len(recs), filename)
			outfile, err := os.Create(filename)
			if err != nil {
				stats <- collector.MakeEvent(collector.FileOutputFailed)
				log.Fatalf("Couldn't create output %s: %s", filename, err)
			}
			if _, err = outfile.Write(buf.Bytes()); err != nil {
				stats <- collector.MakeEvent(collector.FileOutputFailed)
				log.Fatalf("Couldn't write to %s: %s", filename, err)
			}
			stats <- collector.MakeEvent(collector.FileOutputWritten)
		}

		buf.Reset()
		sleep()
	}
}
