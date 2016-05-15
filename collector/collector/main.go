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
)

var (
	kafkaOutputFlag = collector.BoolEnvFlag("kafka-enabled", true,
		"Enable sending data to Kafka.")
	fileOutputFlag = collector.BoolEnvFlag("file-enabled", false,
		"Write chunk-<N>.avro files containing the collected data.")
	topicFlag = collector.StringEnvFlag("topic", "sample_topic", //TODO TEMP
		"Kafka topic to send to")
)

func main() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr,
			"Sends various stats in Metrics Avro format to the provided Kafka service\n")
		fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()

	stats := make(chan collector.StatsEvent)
	go collector.StartStatsLoop(stats)

	kafkaOutputChan := make(chan collector.KafkaMessage)
	if *kafkaOutputFlag {
		go collector.RunKafkaProducer(kafkaOutputChan, stats)
	}

	recordInputChan := make(chan interface{})
	go RunAvroTCPReader(recordInputChan, stats)

	// Wrap buf in an AvroWriter
	codec, err := goavro.NewCodec(metrics_schema.MetricListSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}

	buf := new(bytes.Buffer)
	chunkid := 0
	for {
		// TODO map records to topics, flush to all topics when a timeout is reached
		// Accumulate a chunk of N records from the TCP reader
		recs := make([]interface{}, 0, 10)
		for record := range recordInputChan {
			recs = append(recs, record)
			if len(recs) >= cap(recs) {
				break
			}
		}

		// Recreate OCF writer for each chunk, so that it writes a header at the top each time:
		avroWriter, err := goavro.NewWriter(
			goavro.BlockSize(int64(len(recs))), goavro.ToWriter(buf), goavro.UseCodec(codec))
		if err != nil {
			stats <- collector.StatsEvent{collector.AvroWriterOpenFailed, ""}
			log.Fatal("Failed to create avro writer: ", err)
		}
		for _, rec := range recs {
			stats <- collector.StatsEvent{collector.AvroRecordOut, *topicFlag}
			avroWriter.Write(rec)
		}

		err = avroWriter.Close() // ensure flush to buf occurs before buf is used
		if err != nil {
			stats <- collector.StatsEvent{collector.AvroWriterCloseFailed, ""}
			log.Fatal("Couldn't flush output: ", err)
		}
		if *kafkaOutputFlag {
			log.Printf("Sending avro-formatted stats for %d records to topic '%s'.",
				len(recs), *topicFlag)
			kafkaOutputChan <- collector.KafkaMessage{
				Topic: *topicFlag,
				Data:  buf.Bytes(),
			}
		}
		if *fileOutputFlag {
			filename := fmt.Sprintf("chunk-%d.avro", chunkid)
			chunkid++
			log.Printf("Writing avro-formatted stats for %d records to file '%s'.",
				len(recs), filename)
			outfile, err := os.Create(filename)
			if err != nil {
				stats <- collector.StatsEvent{collector.FileOutputFailed, ""}
				log.Fatalf("Couldn't create output %s: %s", filename, err)
			}
			if _, err = outfile.Write(buf.Bytes()); err != nil {
				stats <- collector.StatsEvent{collector.FileOutputFailed, ""}
				log.Fatalf("Couldn't write to %s: %s", filename, err)
			}
			stats <- collector.StatsEvent{collector.FileOutputWritten, ""}
		}

		buf.Reset()
	}
}
