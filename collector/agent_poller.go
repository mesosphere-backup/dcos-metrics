package collector

import (
	"errors"
	"fmt"
	"github.com/antonholmquist/jason"
	"github.com/linkedin/goavro"
	"github.com/mesosphere/dcos-stats/collector/metrics-schema"
	"io/ioutil"
	"log"
	"os/exec"
	"strings"
	"time"
)

var (
	agentIpCommandFlag = StringEnvFlag("agent-ip-command", "/opt/mesosphere/bin/detect_ip",
		"A command to execute which writes the local Mesos Agent's IP to stdout")
	agentPortFlag = IntEnvFlag("agent-port", 5051,
		"HTTP port to use when querying the local Mesos Agent")
	agentPollPeriodFlag = IntEnvFlag("agent-poll-period", 15,
		"Period between retrievals of data from the local Mesos Agent, in seconds")
	agentMetricsTopicFlag = StringEnvFlag("agent-metrics-topic", "agent",
		"Topic to use for local Mesos Agent statistics. Will be prefixed with -kafka-topic-prefix")
	agentTestStateFileFlag = StringEnvFlag("agent-test-state-file", "",
		"JSON file containing the agent state to be used, for debugging")
	agentTestSystemFileFlag = StringEnvFlag("agent-test-system-file", "",
		"JSON file containing the agent system metrics to be used, for debugging")
	agentTestContainersFileFlag = StringEnvFlag("agent-test-containers-file", "",
		"JSON file containing the container usage metrics to be used, for debugging")

	authCredentialFlag = StringEnvFlag("auth-credential", "",
		"Authentication credential token for use with querying the Mesos Agent")

	datapointNamespace = goavro.RecordEnclosingNamespace(metrics_schema.DatapointNamespace)
	datapointSchema    = goavro.RecordSchema(metrics_schema.DatapointSchema)

	metricListNamespace = goavro.RecordEnclosingNamespace(metrics_schema.MetricListNamespace)
	metricListSchema    = goavro.RecordSchema(metrics_schema.MetricListSchema)

	tagNamespace = goavro.RecordEnclosingNamespace(metrics_schema.TagNamespace)
	tagSchema    = goavro.RecordSchema(metrics_schema.TagSchema)
)

const (
	containerMetricPrefix = "dcos.metrics.container."
	systemMetricPrefix    = "dcos.metrics.agent."

	// same name in both agent json and metrics tags:
	timestampKey   = "timestamp"
	containerIdKey = "container_id"
	executorIdKey  = "executor_id"
	frameworkIdKey = "framework_id"
)

type AgentState struct {
	// agent_id
	agentId string
	// framework_id => framework_name
	frameworkNames map[string]string
}

// Runs an Agent Poller which periodically produces data retrieved from a local Mesos Agent.
// This function should be run as a gofunc.
func RunAgentPoller(recordsChan chan<- *AvroDatum, agentStateChan chan<- *AgentState, stats chan<- StatsEvent) {
	// fetch agent ip once. per DC/OS docs, changing a node IP is unsupported
	var agentIp string = ""
	if len(*agentTestStateFileFlag) == 0 ||
		len(*agentTestSystemFileFlag) == 0 ||
		len(*agentTestContainersFileFlag) == 0 {
		// only get the ip if actually needed
		agentIp = getAgentIp(stats)
	}

	// do one poll immediately upon starting, to ensure that agent metadata is populated early:
	pollAgent(agentIp, recordsChan, agentStateChan, stats)
	ticker := time.NewTicker(time.Second * time.Duration(*agentPollPeriodFlag))
	for {
		select {
		case _ = <-ticker.C:
			pollAgent(agentIp, recordsChan, agentStateChan, stats)
		}
	}
}

// ---

func pollAgent(agentIp string, recordsChan chan<- *AvroDatum, agentStateChan chan<- *AgentState, stats chan<- StatsEvent) {
	// always fetch/emit agent state first: downstream will use it for tagging metrics
	agentState, err := getAgentState(agentIp, stats)
	if err == nil {
		agentStateChan <- agentState
	} else {
		log.Printf("Failed to retrieve state from agent at %s: %s", agentIp, err)
	}

	systemMetricsList, err := getSystemMetrics(agentIp, agentState, stats)
	if err == nil {
		if systemMetricsList != nil {
			recordsChan <- systemMetricsList
		}
	} else {
		log.Printf("Failed to retrieve system metrics from agent at %s: %s", agentIp, err)
	}

	containerMetricsLists, err := getContainerMetrics(agentIp, agentState, stats)
	if err == nil {
		for _, metricList := range containerMetricsLists {
			recordsChan <- metricList
		}
	} else {
		log.Printf("Failed to retrieve container metrics from agent at %s: %s", agentIp, err)
	}
}

// runs detect_ip => "10.0.3.26\n"
func getAgentIp(stats chan<- StatsEvent) string {
	stats <- MakeEvent(AgentIpLookup)
	cmdWithArgs := strings.Split(*agentIpCommandFlag, " ")
	ipBytes, err := exec.Command(cmdWithArgs[0], cmdWithArgs[1:]...).Output()
	if err != nil {
		stats <- MakeEvent(AgentIpLookupFailed)
		log.Fatalf("Fetching Agent IP with -agent-ip-command='%s' failed: %s", *agentIpCommandFlag, err)
	}
	ip := strings.TrimSpace(string(ipBytes))
	if len(ip) == 0 {
		stats <- MakeEvent(AgentIpLookupEmpty)
		log.Fatalf("Agent IP fetched with -agent-ip-command='%s' is empty", *agentIpCommandFlag)
	}
	//log.Printf("Agent IP obtained with -agent-ip-command='%s': %s\n", *agentIpCommandFlag, ip)
	return ip
}

// fetches container-level resource metrics from the agent (via /containers)
func getContainerMetrics(agentIp string, agentState *AgentState, stats chan<- StatsEvent) ([]*AvroDatum, error) {
	rootJson, err := getJsonFromAgent(agentIp, "/containers", agentTestContainersFileFlag, stats)
	if err != nil {
		return nil, err
	}

	containersArray, err := rootJson.ObjectArray()
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	metricLists := make([]*AvroDatum, 0)
	// collect datapoints. expecting a list of dicts, where each dict has:
	// - tags: 'container_id'/'executor_id'/'framework_id' strings
	// - datapoints/timestamp: 'statistics' dict of string=>int/dbl (incl a dbl 'timestamp')
	for _, containerObj := range containersArray {

		statisticsObj, err := containerObj.GetObject("statistics")
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}

		// extract timestamp from statistics
		timestampRaw, err := statisticsObj.GetFloat64(timestampKey)
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		timestampMillis := int64(timestampRaw * 1000)

		// create datapoints from statistics (excluding timestamp itself)
		datapoints := make([]interface{}, 0)
		for key, valRaw := range statisticsObj.Map() {
			if key == timestampKey {
				continue
			}
			valFloat, err := valRaw.Float64()
			if err != nil {
				stats <- MakeEvent(AgentMetricsValueUnsupported)
				log.Printf("Failed to convert value %s to float64: %+v", key, valRaw)
				continue
			}
			datapoint, err := goavro.NewRecord(datapointNamespace, datapointSchema)
			if err != nil {
				log.Fatalf("Failed to create Datapoint record for topic %s (agent %s): %s",
					*agentMetricsTopicFlag, agentState.agentId, err)
			}
			datapoint.Set("name", containerMetricPrefix+key)
			datapoint.Set("time_ms", timestampMillis)
			datapoint.Set("value", valFloat)
			datapoints = append(datapoints, datapoint)
		}
		stats <- MakeEventCount(AgentMetricsValue, len(datapoints))
		if len(datapoints) == 0 {
			// no data, exit early
			continue
		}

		// create tags
		tags := make([]interface{}, 0)
		// container_id
		tagVal, err := containerObj.GetString(containerIdKey)
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		tags = addTag(tags, containerIdKey, tagVal)
		// executor_id
		tagVal, err = containerObj.GetString(executorIdKey)
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		tags = addTag(tags, executorIdKey, tagVal)
		// framework_id
		tagVal, err = containerObj.GetString(frameworkIdKey)
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		tags = addTag(tags, frameworkIdKey, tagVal)

		// note: agent_id/framework_name tags are automatically added downstream
		metricListRec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
		if err != nil {
			log.Fatalf("Failed to create MetricList record for topic %s (agent %s): %s",
				*agentMetricsTopicFlag, agentState.agentId, err)
		}
		metricListRec.Set("topic", *agentMetricsTopicFlag)
		metricListRec.Set("datapoints", datapoints)
		metricListRec.Set("tags", tags)
		// just use a size of zero, relative to limits it'll be insignificant anyway:
		metricLists = append(metricLists, &AvroDatum{metricListRec, *agentMetricsTopicFlag, 0})
	}
	return metricLists, nil
}

// fetches system-level metrics from the agent (via /metrics/snapshot)
func getSystemMetrics(agentIp string, agentState *AgentState, stats chan<- StatsEvent) (*AvroDatum, error) {
	rootJson, err := getJsonFromAgent(agentIp, "/metrics/snapshot", agentTestSystemFileFlag, stats)
	if err != nil {
		return nil, err
	}

	json, err := rootJson.Object()
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	nowMillis := time.Now().UnixNano() / 1000000
	// collect datapoints
	// expecting a single dict containing 'string => floatval' entries
	datapoints := make([]interface{}, 0)
	for key, valRaw := range json.Map() {
		valFloat, err := valRaw.Float64()
		if err != nil {
			stats <- MakeEvent(AgentMetricsValueUnsupported)
			log.Printf("Failed to convert value %s to float64: %+v", key, valRaw)
			continue
		}
		datapoint, err := goavro.NewRecord(datapointNamespace, datapointSchema)
		if err != nil {
			log.Fatalf("Failed to create Datapoint record for topic %s (agent %s): %s",
				*agentMetricsTopicFlag, agentState.agentId, err)
		}
		datapoint.Set("name", systemMetricPrefix+strings.Replace(key, "/", ".", -1)) // "key/path" => "key.path"
		datapoint.Set("time_ms", nowMillis)
		datapoint.Set("value", valFloat)
		datapoints = append(datapoints, datapoint)
	}
	stats <- MakeEventCount(AgentMetricsValue, len(datapoints))
	if len(datapoints) == 0 {
		return nil, errors.New("No datapoints found in agent metrics")
	}

	metricListRec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
	if err != nil {
		log.Fatalf("Failed to create MetricList record for topic %s (agent %s): %s",
			*agentMetricsTopicFlag, agentState.agentId, err)
	}
	metricListRec.Set("topic", *agentMetricsTopicFlag)
	metricListRec.Set("datapoints", datapoints)
	// note: agent_id tag is automatically added downstream
	metricListRec.Set("tags", make([]interface{}, 0))
	// just use a size of zero, relative to limits it'll be insignificant anyway:
	return &AvroDatum{metricListRec, *agentMetricsTopicFlag, 0}, nil
}

func getAgentState(agentIp string, stats chan<- StatsEvent) (*AgentState, error) {
	rootJson, err := getJsonFromAgent(agentIp, "/state", agentTestStateFileFlag, stats)
	if err != nil {
		return nil, err
	}

	json, err := rootJson.Object()
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	// state["id"] (agent_id)
	agentId, err := json.GetString("id")
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	// state["frameworks"][0]["id"] (framework_id) => state["frameworks"][0]["name"] (framework_name)
	frameworks, err := json.GetObjectArray("frameworks")
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}
	frameworkNames := make(map[string]string, len(frameworks))
	for _, framework := range frameworks {
		frameworkId, err := framework.GetString("id")
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		frameworkName, err := framework.GetString("name")
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		frameworkNames[frameworkId] = frameworkName
	}

	return &AgentState{agentId, frameworkNames}, nil
}

func getJsonFromAgent(agentIp string, urlPath string, testFileFlag *string, stats chan<- StatsEvent) (*jason.Value, error) {
	stats <- MakeEvent(AgentQuery)
	var rawJson []byte = nil
	var err error = nil
	if len(*testFileFlag) == 0 {
		endpoint := fmt.Sprintf("http://%s:%d%s", agentIp, *agentPortFlag, urlPath)
		if len(*authCredentialFlag) == 0 {
			rawJson, err = HttpGet(endpoint)
		} else {
			rawJson, err = AuthedHttpGet(endpoint, *authCredentialFlag)
		}
		// Special case: on HTTP 401 Unauthorized, exit immediately rather than failing forever
		if httpErr, ok := err.(HttpCodeError); ok {
			if httpErr.Code == 401 {
				stats <- MakeEvent(AgentQueryFailed)
				log.Fatalf("Got 401 Unauthorized when querying agent. "+
					"Please provide a suitable auth token using the AUTH_CREDENTIAL env var: %s", err)
			}
		}
	} else {
		rawJson, err = ioutil.ReadFile(*testFileFlag)
	}
	if err != nil {
		stats <- MakeEvent(AgentQueryFailed)
		return nil, err
	}

	json, err := jason.NewValueFromBytes(rawJson)
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}
	return json, nil
}
