package main

import (
	"bytes"
	"flag"
	"fmt"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector"
	"github.com/Shopify/sarama"
	"log"
	"os"
	"strings"
	"time"
)

var (
	brokersFlag = flag.String("brokers", os.Getenv("KAFKA_PEERS"), "The Kafka brokers to connect to, as a comma separated list.")
	frameworkFlag = flag.String("framework", "", "The Kafka framework to query for brokers. (overrides '-brokers')")
	kafkaOutputFlag = flag.Bool("kafka", true, "Enable sending data to Kafka.")
	fileOutputFlag = flag.Bool("file", false, "Write chunk-N.avro files containing generated chunks.")
	topicFlag = flag.String("topic", "sample_metrics", "The Kafka topic to write data against.")
	verboseFlag = flag.Bool("verbose", false, "Turn on extra logging.")
	chunkCountFlag = flag.Int("count", 5, "Number of chunks to send.")
	chunkSizeFlag = flag.Int("size", 3, "Size of each chunk to be sent.")
)

func newRandRecord(schema goavro.RecordSetter) *goavro.Record {
    rec, err := goavro.NewRecord(
		schema,
		goavro.RecordEnclosingNamespace("dcos.metrics"))
    if err != nil {
        log.Fatal("Failed to create record from schema: ", err)
    }
	rec.Set("topic", *topicFlag)
	rec.Set("tags", make([]interface{}, 0))
	rec.Set("metrics", make([]interface{}, 0))
	// TODO populate with some nice-looking random data...
	return rec
}

func newAsyncProducer(brokerList []string) sarama.AsyncProducer {
	// For the access log, we are looking for AP semantics, with high throughput.
	// By creating batches of compressed messages, we reduce network I/O at a cost of more latency.
	config := sarama.NewConfig()
	config.Producer.RequiredAcks = sarama.WaitForLocal       // Only wait for the leader to ack
	config.Producer.Compression = sarama.CompressionSnappy   // Compress messages
	config.Producer.Flush.Frequency = 500 * time.Millisecond // Flush batches every 500ms

	producer, err := sarama.NewAsyncProducer(brokerList, config)
	if err != nil {
		log.Fatal("Failed to start Kafka producer: ", err)
	}
	go func() {
		for err := range producer.Errors() {
			log.Println("Failed to write metrics to Kafka:", err)
		}
	}()

	return producer
}

func getBrokers() []string {
	if *frameworkFlag != "" {
		brokerList, err := collector.LookupBrokers(*frameworkFlag)
		if err != nil {
			log.Fatal("Broker lookup failed: ", err)
		}
		return brokerList
	} else if *brokersFlag != "" {
		brokerList := strings.Split(*brokersFlag, ",")
		if len(brokerList) == 0 {
			log.Fatalf("No brokers in list: ", *brokersFlag)
		}
		return brokerList
	} else {
		flag.Usage()
		log.Fatal("Either -framework or -brokers must be specified, or -kafka must be false.")
	}
	return nil;
}

func main() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr, "Sends various stats in Metrics Avro format to the provided Kafka service\n")
		fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()

	if *verboseFlag {
		sarama.Logger = log.New(os.Stdout, "[sarama] ", log.LstdFlags)
	}

	var kafkaProducer sarama.AsyncProducer
	if *kafkaOutputFlag {
		brokers := getBrokers()
		log.Printf("Kafka brokers: %s", strings.Join(brokers, ", "))
		kafkaProducer = newAsyncProducer(brokers)
		defer func() {
			if err := kafkaProducer.Close(); err != nil {
				log.Println("Failed to shut down producer cleanly", err)
			}
		}()
	}

	// Wrap buf in an AvroWriter
	buf := new(bytes.Buffer)
	codec, err := goavro.NewCodec(collector.MetricsSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}

	recordSchema := goavro.RecordSchema(collector.MetricsSchema)
	for ichunk := 0; ichunk < *chunkCountFlag; ichunk++ {
		// Recreate OCF writer for each chunk, so that it writes a header at the top each time:
		avroWriter, err := goavro.NewWriter(
			goavro.BlockSize(int64(*chunkSizeFlag)),
			goavro.ToWriter(buf),
			goavro.UseCodec(codec))
		if err != nil {
			log.Fatal("Failed to create avro writer: ", err)
		}

		for irec := 0; irec < *chunkSizeFlag; irec++ {
			record := newRandRecord(recordSchema)
			avroWriter.Write(record)
		}

		avroWriter.Close() // ensure flush to buf
		if *kafkaOutputFlag {
			kafkaProducer.Input() <- &sarama.ProducerMessage{
				Topic: *topicFlag,
				Value: sarama.ByteEncoder(buf.Bytes()),
			}
		}
		if *fileOutputFlag {
			filename := fmt.Sprintf("chunk-%d.avro", ichunk)
			outfile, err := os.Create(filename)
			if err != nil {
				log.Fatalf("Couldn't create output %s: %s", filename, err)
			}
			_, err = outfile.Write(buf.Bytes())
			if err != nil {
				log.Fatalf("Couldn't write to %s: %s", filename, err)
			}
		}
		buf.Reset()
	}
}
