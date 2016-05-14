package main

import (
	"bytes"
	"flag"
	"fmt"
	"github.com/Shopify/sarama"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"log"
	"os"
	"time"
)

var (
	kafkaOutputFlag = flag.Bool("kafka", collector.EnvBool("KAFKA_ENABLED", true),
		"Enable sending data to Kafka.")
	fileOutputFlag = flag.Bool("file", collector.EnvBool("FILE_OUT", false),
		"Write chunk-N.avro files containing generated chunks.")
	topicFlag = flag.String("topic", collector.EnvString("KAFKA_TOPIC", "sample_metrics"),
		"The Kafka topic to write data against.")
	verboseFlag = flag.Bool("verbose", collector.EnvBool("VERBOSE", false),
		"Turn on extra logging.")
	pollPeriodFlag = flag.Int("period", collector.EnvInt("POLL_PERIOD", 15),
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

	if *verboseFlag {
		sarama.Logger = log.New(os.Stdout, "[sarama] ", log.LstdFlags)
	}

	var kafkaProducer sarama.AsyncProducer
	var err error
	if *kafkaOutputFlag {
		for true {
			kafkaProducer, err = collector.GetKafkaProducer()
			if err == nil {
				break
			}
			fmt.Fprintf(os.Stderr, "%s\n", err)
			sleep() // sleep and retry
		}
	}
	defer func() {
		if err := kafkaProducer.Close(); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to shut down producer cleanly\n", err)
		}
	}()

	// Wrap buf in an AvroWriter
	buf := new(bytes.Buffer)
	codec, err := goavro.NewCodec(metrics_schema.MetricListSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}

	agentIp, err := collector.AgentGetIp()
	if err != nil {
		log.Fatal("Failed to get agent IP: ", err)
	}
	chunkid := 0
	for true {
		// Get stats from agent
		recs, err := collector.AgentStatisticsAvro(agentIp, *topicFlag)
		if err != nil {
			fmt.Fprintf(os.Stderr,
				"Failed to fetch/parse stats from agent at %s: %s\n", agentIp, err)
			// sleep and retry
			sleep()
			continue
		} else if len(recs) == 0 {
			log.Printf("No containers returned in stats, trying again in %ds\n", *pollPeriodFlag)
			// sleep and retry
			sleep()
			continue
		}

		// Recreate OCF writer for each chunk, so that it writes a header at the top each time:
		avroWriter, err := goavro.NewWriter(
			goavro.BlockSize(int64(len(recs))), goavro.ToWriter(buf), goavro.UseCodec(codec))
		if err != nil {
			log.Fatal("Failed to create avro writer: ", err)
		}
		for _, rec := range recs {
			avroWriter.Write(rec)
		}

		err = avroWriter.Close() // ensure flush to buf occurs before buf is used
		if err != nil {
			log.Fatal("Couldn't flush output: ", err)
		}
		if *kafkaOutputFlag {
			log.Printf("Sending avro-formatted stats for %d containers.", len(recs))
			kafkaProducer.Input() <- &sarama.ProducerMessage{
				Topic: *topicFlag,
				Value: sarama.ByteEncoder(buf.Bytes()),
			}
		}
		if *fileOutputFlag {
			log.Printf("Sending avro-formatted stats for %d containers.", len(recs))
			filename := fmt.Sprintf("chunk-%d.avro", chunkid)
			chunkid++
			outfile, err := os.Create(filename)
			if err != nil {
				log.Fatalf("Couldn't create output %s: %s", filename, err)
			}
			if _, err = outfile.Write(buf.Bytes()); err != nil {
				log.Fatalf("Couldn't write to %s: %s", filename, err)
			}
		}

		buf.Reset()
		sleep()
	}
}
