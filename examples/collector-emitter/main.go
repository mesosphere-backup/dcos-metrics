package main

// 'go generate' must be run for the 'metrics-schema' package to be present:
//go:generate go run ../../schema/go/generator.go -infile ../../schema/metrics.avsc -outfile metrics-schema/schema.go

import (
	"flag"
	"fmt"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/examples/collector-emitter/metrics-schema"
	"log"
	"net"
	"os"
	"runtime"
	"strconv"
	"time"
)

var (
	sendEndpointFlag = flag.String("endpoint", "127.0.0.1:8124",
		"TCP endpoint for outgoing MetricsList avro data")
	topicFlag = flag.String("topic", "collector-client",
		"Topic to use for sent avro records")
	recordOutputLogFlag = flag.Bool("record-output-log", false,
		"Whether to log the parsed content of outgoing records")
	sendPeriodFlag = flag.Int("period", 1, "Seconds to wait between stats refreshes")
	blockSizeFlag  = flag.Int("block-size", 10,
		"Maximum number of records to include in an avro block (must be >0)")
	blockTickMsFlag = flag.Int("block-tick-ms", 500,
		"Number of milliseconds to wait before flushing the current avro block (0 = disabled)")

	datapointNamespace = goavro.RecordEnclosingNamespace(metrics_schema.DatapointNamespace)
	datapointSchema    = goavro.RecordSchema(metrics_schema.DatapointSchema)

	metricListNamespace = goavro.RecordEnclosingNamespace(metrics_schema.MetricListNamespace)
	metricListSchema    = goavro.RecordSchema(metrics_schema.MetricListSchema)

	tagNamespace = goavro.RecordEnclosingNamespace(metrics_schema.TagNamespace)
	tagSchema    = goavro.RecordSchema(metrics_schema.TagSchema)

	startTime = time.Now()
)

func main() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr,
			"Sends various stats in Metrics Avro format to the provided Collector endpoint\n")
		fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()

	recordsChan := make(chan []interface{})
	go runTCPSerializerSender(recordsChan)
	generateStats(recordsChan)
}

func sleep(note string) {
	log.Printf("Waiting for %ds before %s\n", *sendPeriodFlag, note)
	time.Sleep(time.Duration(*sendPeriodFlag) * time.Second)
}

// ---

func generateStats(recordsChan chan<- []interface{}) {
	for {
		// Send two MetricLists, each with a different set of tags.
		// In practice these would probably be combined into a single MetricList.
		//
		now := time.Now()
		timeMs := now.UnixNano() / 1000 / 1000

		// Tags are information identifying the sender, eg the machine/container it's running on.
		hostname, err := os.Hostname()
		if err != nil {
			hostname = "UNKNOWN"
		}
		tags := []interface{}{
			buildTag("hostname", hostname),
			buildTag("pid", strconv.Itoa(os.Getpid())),
		}

		// The metrics themselves are values with timestamps attached.
		var memstats runtime.MemStats
		runtime.ReadMemStats(&memstats)
		datapoints := []interface{}{
			buildDatapoint("time.unix", timeMs, float64(now.Unix())),
			buildDatapoint("time.unix_ms", timeMs, float64(timeMs)),
			buildDatapoint("time.uptime_ms", timeMs,
				float64((now.UnixNano()-startTime.UnixNano())/1000/1000)),
			buildDatapoint("mem.alloc", timeMs, float64(memstats.Alloc)),
			buildDatapoint("mem.total_alloc", timeMs, float64(memstats.TotalAlloc)),
			buildDatapoint("mem.sys", timeMs, float64(memstats.Sys)),
			buildDatapoint("mem.lookups", timeMs, float64(memstats.Lookups)),
			buildDatapoint("mem.mallocs", timeMs, float64(memstats.Mallocs)),
			buildDatapoint("mem.frees", timeMs, float64(memstats.Frees)),
			buildDatapoint("proc.pid", timeMs, float64(os.Getpid())),
			buildDatapoint("proc.uid", timeMs, float64(os.Getuid())),
		}

		// We could sent multiple MetricLists, each with different tags.
		metriclists := []interface{}{buildMetricList(*topicFlag, tags, datapoints)}
		log.Printf("Sending %d Datapoints across %d MetricLists", len(datapoints), len(metriclists))
		recordsChan <- metriclists
		sleep("generating more data")
	}
}

func buildMetricList(topic string, tags, datapoints []interface{}) interface{} {
	metricList, err := goavro.NewRecord(metricListNamespace, metricListSchema)
	if err != nil {
		log.Fatal("Failed to create MetricList record for topic %s: %s", topic, err)
	}
	metricList.Set("topic", topic)
	metricList.Set("tags", tags)
	metricList.Set("datapoints", datapoints)
	return metricList
}

func buildTag(key, value string) interface{} {
	tag, err := goavro.NewRecord(tagNamespace, tagSchema)
	if err != nil {
		log.Fatal("Failed to create Tag record: ", err)
	}
	tag.Set("key", key)
	tag.Set("value", value)
	return tag
}

func buildDatapoint(name string, timeMs int64, value float64) interface{} {
	datapoint, err := goavro.NewRecord(datapointNamespace, datapointSchema)
	if err != nil {
		log.Fatalf("Failed to create Datapoint record for value %s: %s", name, err)
	}
	datapoint.Set("name", name)
	datapoint.Set("time_ms", timeMs)
	datapoint.Set("value", value)
	return datapoint
}

// ---

func runTCPSerializerSender(recordsChan <-chan []interface{}) {
	codec, err := goavro.NewCodec(metrics_schema.MetricListSchema)
	if err != nil {
		log.Fatal("Failed to initialize avro codec: ", err)
	}
	// (Re-)Open connection
	for {
		log.Println("Connecting to endpoint", *sendEndpointFlag)
		addr, err := net.ResolveTCPAddr("tcp", *sendEndpointFlag)
		if err != nil {
			log.Fatalf("Failed to parse TCP endpoint '%s': %s", *sendEndpointFlag, err)
		}
		sock, err := net.DialTCP("tcp", nil, addr)
		if err != nil {
			log.Printf("Failed to connect to TCP endpoint '%s', retrying: %s\n", *sendEndpointFlag, err)
			sleep(fmt.Sprintf("reconnecting to endpoint '%s'", *sendEndpointFlag))
			continue
		}
		defer func() {
			if err := sock.Close(); err != nil {
				log.Printf("Failed to close TCP socket to endpoint '%s': %s\n", *sendEndpointFlag, err)
			}
		}()

		// Recreate OCF writer once per TCP session. The OCF file header must be (re-)sent whenever
		// the session is (re-)opened. Writer defaults to 10 items per record block.
		writer := &TCPWriterProxy{sock, nil}
		avroWriter, err := goavro.NewWriter(goavro.ToWriter(writer), goavro.UseCodec(codec),
			// flush a block when it reaches this size...:
			goavro.BlockSize(int64(*blockSizeFlag)),
			// ... or when this much time has passed:
			goavro.BlockTick(time.Duration(*blockTickMsFlag)*time.Millisecond))
		if err != nil {
			log.Fatalf("Failed to create Avro writer: ", err)
		}

		// Send data until the connection is lost (detected via TCPWriterProxy)
		for {
			for _, rec := range <-recordsChan {
				if *recordOutputLogFlag {
					log.Println("RECORD OUT:", rec)
				}
				avroWriter.Write(rec)
			}
			if writer.lastErr != nil {
				log.Printf("Failed to write to TCP socket for endpoint '%s': %s\n",
					*sendEndpointFlag, writer.lastErr)
				break
			}
		}

		err = avroWriter.Close()
		if err != nil {
			log.Println("Failed to close Avro writer (this is expected if socket died): ", err)
		}

		sleep(fmt.Sprintf("reconnecting to endpoint '%s'", *sendEndpointFlag))
	}
}

// An io.Writer which stores the error when a write fails
type TCPWriterProxy struct {
	sock    *net.TCPConn
	lastErr error
}

func (self *TCPWriterProxy) Write(b []byte) (int, error) {
	n, err := self.sock.Write(b)
	if err != nil {
		log.Println("Intercepted TCP Write failure:", err)
		self.lastErr = err
		return 0, err
	}
	log.Printf("Wrote %d bytes to endpoint '%s'\n", n, *sendEndpointFlag)
	return n, nil
}
