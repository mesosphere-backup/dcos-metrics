package main

import (
	"bytes"
	"errors"
	"flag"
	"fmt"
	"github.com/Shopify/sarama"
	"github.com/antonholmquist/jason"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"
	"strings"
	"time"
)

var (
	brokersFlag     = flag.String("brokers", os.Getenv("KAFKA_PEERS"), "The Kafka brokers to connect to, as a comma separated list.")
	frameworkFlag   = flag.String("framework", "", "The Kafka framework to query for brokers. (overrides '-brokers')")
	kafkaOutputFlag = flag.Bool("kafka", true, "Enable sending data to Kafka.")
	fileOutputFlag  = flag.Bool("file", false, "Write chunk-N.avro files containing generated chunks.")
	topicFlag       = flag.String("topic", "sample_metrics", "The Kafka topic to write data against.")
	verboseFlag     = flag.Bool("verbose", false, "Turn on extra logging.")
	pollPeriodFlag  = flag.Int("period", 15, "Seconds to wait between stats refreshes")
	ipCommandFlag   = flag.String("ipcmd", "/opt/mesosphere/bin/detect_ip", "A command to execute which writes the agent IP to stdout")

	datapointNamespace = goavro.RecordEnclosingNamespace(metrics_schema.DatapointNamespace)
	datapointSchema    = goavro.RecordSchema(metrics_schema.DatapointSchema)

	metricListNamespace = goavro.RecordEnclosingNamespace(metrics_schema.MetricListNamespace)
	metricListSchema    = goavro.RecordSchema(metrics_schema.MetricListSchema)

	metricNamespace = goavro.RecordEnclosingNamespace(metrics_schema.MetricNamespace)
	metricSchema    = goavro.RecordSchema(metrics_schema.MetricSchema)

	tagNamespace = goavro.RecordEnclosingNamespace(metrics_schema.TagNamespace)
	tagSchema    = goavro.RecordSchema(metrics_schema.TagSchema)
)

// run detect_ip => "10.0.3.26\n"
func getAgentIp() (ip string, err error) {
	cmdWithArgs := strings.Split(*ipCommandFlag, " ")
	ipBytes, err := exec.Command(cmdWithArgs[0], cmdWithArgs[1:]...).Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(ipBytes)), nil
}

func httpGet(endpoint string) (body []byte, err error) {
	response, err := http.Get(endpoint)
	if err != nil {
		return nil, err
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return nil, errors.New(fmt.Sprintf(
			"Got response code when querying %s: %d", endpoint, response.StatusCode))
	}
	return ioutil.ReadAll(response.Body)
}

func convertJsonStatistics(rawJson []byte) (recs []*goavro.Record, err error) {
	parsedJson, err := jason.NewValueFromBytes(rawJson)
	if err != nil {
		return nil, err
	}
	containers, err := parsedJson.ObjectArray()
	if err != nil {
		return nil, err
	}
	recs = make([]*goavro.Record, len(containers))
	for i, container := range containers {
		if err != nil {
			log.Fatal("Failed to create MetricsList record: ", err)
		}
		tags := make([]interface{}, 0)
		metrics := make([]interface{}, 0)
		for entrykey, entryval := range container.Map() {
			// try as string
			strval, err := entryval.String()
			if err == nil {
				// it's a string value. treat it as a tag.
				tag, err := goavro.NewRecord(tagNamespace, tagSchema)
				if err != nil {
					log.Fatal("Failed to create Tag record: ", err)
				}
				tag.Set("key", entrykey)
				tag.Set("value", strval)
				tags = append(tags, tag)
				continue
			}
			// try as object
			objval, err := entryval.Object()
			if err != nil {
				fmt.Fprint(os.Stderr, "JSON Value %s isn't a string nor an object\n", entrykey)
				continue
			}
			// it's an object, treat it as a list of floating-point metrics (with a timestamp val)
			timestampFloat, err := objval.GetFloat64("timestamp")
			if err != nil {
				fmt.Fprint(os.Stderr, "Expected 'timestamp' int value in JSON Value %s\n", entrykey)
				continue // skip bad value
			}
			timestampMillis := int64(timestampFloat * 1000)
			for key, val := range objval.Map() {
				// treat as float, with single datapoint
				if key == "timestamp" {
					continue // avoid being too redundant
				}
				datapoint, err := goavro.NewRecord(datapointNamespace, datapointSchema)
				if err != nil {
					log.Fatalf("Failed to create Datapoint record for value %s: %s", key, err)
				}
				datapoint.Set("time", timestampMillis)
				floatVal, err := val.Float64()
				if err != nil {
					fmt.Fprintf(os.Stderr, "Failed to convert value %s to float64: %+v\n", key, val)
					continue
				}
				datapoint.Set("value", floatVal)

				metric, err := goavro.NewRecord(metricNamespace, metricSchema)
				metric.Set("name", key)
				metric.Set("datapoints", []interface{}{datapoint})
				metrics = append(metrics, metric)
			}
		}
		metricListRec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
		if err != nil {
			log.Fatal("Failed to create MetricList record: %s", err)
		}
		metricListRec.Set("topic", *topicFlag)
		metricListRec.Set("tags", tags)
		metricListRec.Set("metrics", metrics)
		recs[i] = metricListRec
	}
	return recs, nil
}

func newAsyncProducer(brokerList []string) (producer sarama.AsyncProducer, err error) {
	// For the access log, we are looking for AP semantics, with high throughput.
	// By creating batches of compressed messages, we reduce network I/O at a cost of more latency.
	config := sarama.NewConfig()
	config.Producer.RequiredAcks = sarama.WaitForLocal       // Only wait for the leader to ack
	config.Producer.Compression = sarama.CompressionSnappy   // Compress messages
	config.Producer.Flush.Frequency = 500 * time.Millisecond // Flush batches every 500ms

	producer, err = sarama.NewAsyncProducer(brokerList, config)
	if err != nil {
		return nil, err
	}
	go func() {
		for err := range producer.Errors() {
			log.Println("Failed to write metrics to Kafka:", err)
		}
	}()
	return producer, nil
}

func sleep() {
	log.Printf("Wait for %ds...\n", *pollPeriodFlag)
	time.Sleep(time.Duration(*pollPeriodFlag) * time.Second)
}

func getKafkaProducer() sarama.AsyncProducer {
	for true {
		brokers := make([]string, 0)
		if *frameworkFlag != "" {
			foundBrokers, err := collector.LookupBrokers(*frameworkFlag)
			brokers = append(brokers, foundBrokers...)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Broker lookup failed: %s\n", err)
				// sleep and retry
				sleep()
				continue
			}
		} else if *brokersFlag != "" {
			brokers = strings.Split(*brokersFlag, ",")
			if len(brokers) == 0 {
				log.Fatal("-brokers must be non-empty.")
			}
		} else {
			flag.Usage()
			log.Fatal("Either -framework or -brokers must be specified, or -kafka must be false.")
		}
		log.Printf("Kafka brokers: %s", strings.Join(brokers, ", "))

		kafkaProducer, err := newAsyncProducer(brokers)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Producer creation against brokers %+v failed: %s\n", brokers, err)
			sleep()
			continue
		}
		return kafkaProducer
	}
	return nil // happy compiler
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
		kafkaProducer = getKafkaProducer()
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

	agentIp, err := getAgentIp()
	if err != nil {
		log.Fatal("Failed to get agent IP: ", err)
	}
	if len(agentIp) == 0 {
		log.Fatal("Agent IP is empty")
	}
	chunkid := 0
	for true {
		// Get/parse stats from agent
		rawJson, err := httpGet(fmt.Sprintf("http://%s:5051/monitor/statistics.json", agentIp))
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to retrieve stats from agent at %s: %s\n", agentIp, err)
			// sleep and retry
			sleep()
			continue
		}
		recs, err := convertJsonStatistics(rawJson)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to parse stats from agent: %s\n", err)
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
