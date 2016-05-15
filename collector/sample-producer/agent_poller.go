package main

import (
	"errors"
	"fmt"
	"github.com/antonholmquist/jason"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"log"
	"os/exec"
	"strings"
)

var (
	ipCommandFlag = collector.StringEnvFlag("detect-ip", "/opt/mesosphere/bin/detect_ip",
		"A command to execute which writes the agent IP to stdout")

	datapointNamespace = goavro.RecordEnclosingNamespace(metrics_schema.DatapointNamespace)
	datapointSchema    = goavro.RecordSchema(metrics_schema.DatapointSchema)

	metricListNamespace = goavro.RecordEnclosingNamespace(metrics_schema.MetricListNamespace)
	metricListSchema    = goavro.RecordSchema(metrics_schema.MetricListSchema)

	tagNamespace = goavro.RecordEnclosingNamespace(metrics_schema.TagNamespace)
	tagSchema    = goavro.RecordSchema(metrics_schema.TagSchema)
)

func convertJsonStatistics(rawJson []byte, recordTopic string) (recs []*goavro.Record, err error) {
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
		datapoints := make([]interface{}, 0)
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
				log.Printf("JSON Value %s isn't a string nor an object", entrykey)
				continue
			}
			// it's an object, treat it as a list of floating-point metrics (with a timestamp val)
			timestampFloat, err := objval.GetFloat64("timestamp")
			if err != nil {
				log.Printf("Expected 'timestamp' int value in JSON Value %s", entrykey)
				continue // skip bad value
			}
			timestampMillis := int64(timestampFloat * 1000)
			for key, val := range objval.Map() {
				// treat as float, with single datapoint
				if key == "timestamp" {
					continue // avoid being too redundant
				}
				datapoint, err := goavro.NewRecord(datapointNamespace, datapointSchema)
				datapoint.Set("name", key)
				if err != nil {
					log.Fatalf("Failed to create Datapoint record for value %s: %s", key, err)
				}
				datapoint.Set("time", timestampMillis)
				floatVal, err := val.Float64()
				if err != nil {
					log.Printf("Failed to convert value %s to float64: %+v", key, val)
					continue
				}
				datapoint.Set("value", floatVal)
				datapoints = append(datapoints, datapoint)
			}
		}
		metricListRec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
		if err != nil {
			log.Fatal("Failed to create MetricList record: %s", err)
		}
		metricListRec.Set("topic", recordTopic)
		metricListRec.Set("tags", tags)
		metricListRec.Set("datapoints", datapoints)
		recs[i] = metricListRec
	}
	return recs, nil
}

// run detect_ip => "10.0.3.26\n"
func AgentGetIp() (ip string, err error) {
	cmdWithArgs := strings.Split(*ipCommandFlag, " ")
	ipBytes, err := exec.Command(cmdWithArgs[0], cmdWithArgs[1:]...).Output()
	if err != nil {
		return "", errors.New(fmt.Sprintf(
			"Fetching Agent IP with -ip-command='%s' failed: %s", *ipCommandFlag, err))
	}
	ip = strings.TrimSpace(string(ipBytes))
	if len(ip) == 0 {
		return "", errors.New(fmt.Sprintf(
			"Agent IP fetched with -ip-command='%s' is empty", *ipCommandFlag))
	}
	return ip, nil
}

func AgentStatisticsAvro(agentIp string, recordTopic string) (recs []*goavro.Record, err error) {
	// Get/parse stats from agent
	rawJson, err := collector.HttpGet(fmt.Sprintf("http://%s:5051/monitor/statistics.json", agentIp))
	if err != nil {
		return nil, err
	}
	return convertJsonStatistics(rawJson, recordTopic)
}
