package collector

import (
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
	agentTestMetricsFileFlag = StringEnvFlag("agent-test-metrics-file", "",
		"JSON file containing the agent metrics to be used, for debugging")

	datapointNamespace = goavro.RecordEnclosingNamespace(metrics_schema.DatapointNamespace)
	datapointSchema    = goavro.RecordSchema(metrics_schema.DatapointSchema)

	metricListNamespace = goavro.RecordEnclosingNamespace(metrics_schema.MetricListNamespace)
	metricListSchema    = goavro.RecordSchema(metrics_schema.MetricListSchema)

	tagNamespace = goavro.RecordEnclosingNamespace(metrics_schema.TagNamespace)
	tagSchema    = goavro.RecordSchema(metrics_schema.TagSchema)
)

type AgentState struct {
	// agent_id
	agentId string
	// framework_id => framework_name
	frameworkNames map[string]string
}

// Runs an Agent Poller which periodically produces data retrieved from a local Mesos Agent.
// This function should be run as a gofunc.
func RunAgentPoller(recordsChan chan<- interface{}, agentStateChan chan<- *AgentState, stats chan<- StatsEvent) {
	ticker := time.NewTicker(time.Second * time.Duration(*agentPollPeriodFlag))
	for {
		select {
		case _ = <-ticker.C:
			// refresh agent ip (probably don't need to refresh constantly, but just in case..)
			var agentIp string = ""
			if len(*agentTestMetricsFileFlag) == 0 || len(*agentTestStateFileFlag) == 0 {
				// only get the ip if actually needed
				agentIp = getAgentIp(stats)
			}
			// fetch/emit agent state first: downstream can use it when handling agent metrics
			agentState, err := getAgentState(agentIp, stats)
			if err == nil {
				agentStateChan <- agentState
			} else {
				log.Printf("Failed to retrieve state from agent at %s: %s", agentIp, err)
			}
			agentMetrics, err := getAgentMetrics(agentIp, agentState, stats)
			if err == nil {
				if agentMetrics != nil {
					recordsChan <- agentMetrics
				}
			} else {
				log.Printf("Failed to retrieve metrics from agent at %s: %s", agentIp, err)
			}
		}
	}
}

// ---

// run detect_ip => "10.0.3.26\n"
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

func getAgentMetrics(agentIp string, agentState *AgentState, stats chan<- StatsEvent) (interface{}, error) {
	stats <- MakeEvent(AgentQuery)
	var rawJson []byte = nil
	var err error = nil
	if len(*agentTestMetricsFileFlag) == 0 {
		rawJson, err = HttpGet(fmt.Sprintf("http://%s:%d/metrics/snapshot", agentIp, *agentPortFlag))
	} else {
		rawJson, err = ioutil.ReadFile(*agentTestMetricsFileFlag)
	}
	if err != nil {
		stats <- MakeEvent(AgentQueryFailed)
		return nil, err
	}

	parsedJson, err := jason.NewValueFromBytes(rawJson)
	if err != nil {
		stats <- MakeEvent(AgentQueryBadMetrics)
		return nil, err
	}
	metrics, err := parsedJson.Object()
	if err != nil {
		stats <- MakeEvent(AgentQueryBadMetrics)
		return nil, err
	}

	nowMillis := time.Now().UnixNano() / 1000000
	// collect datapoints
	// expecting a single dict containing 'string => floatval' entries
	datapoints := make([]interface{}, 0)
	for key, valRaw := range metrics.Map() {
		valFloat, err := valRaw.Float64()
		if err != nil {
			stats <- MakeEvent(AgentMetricsValueUnsupported)
			log.Printf("Failed to convert value %s to float64: %+v", key, valRaw)
			continue
		}
		datapoint, err := goavro.NewRecord(datapointNamespace, datapointSchema)
		datapoint.Set("name", strings.Replace(key, "/", ".", -1)) // "key/path" => "key.path"
		datapoint.Set("time_ms", nowMillis)
		datapoint.Set("value", valFloat)
		datapoints = append(datapoints, datapoint)
	}
	stats <- MakeEventCount(AgentMetricsValue, len(datapoints))
	if len(datapoints) == 0 {
		// no data, exit early
		return nil, nil
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
	return metricListRec, nil
}

func getAgentState(agentIp string, stats chan<- StatsEvent) (*AgentState, error) {
	stats <- MakeEvent(AgentQuery)
	var rawJson []byte = nil
	var err error = nil
	if len(*agentTestStateFileFlag) == 0 {
		rawJson, err = HttpGet(fmt.Sprintf("http://%s:%d/state", agentIp, *agentPortFlag))
	} else {
		rawJson, err = ioutil.ReadFile(*agentTestStateFileFlag)
	}
	if err != nil {
		stats <- MakeEvent(AgentQueryFailed)
		return nil, err
	}

	parsedJson, err := jason.NewValueFromBytes(rawJson)
	if err != nil {
		stats <- MakeEvent(AgentQueryBadState)
		return nil, err
	}
	state, err := parsedJson.Object()
	if err != nil {
		stats <- MakeEvent(AgentQueryBadState)
		return nil, err
	}

	// state["id"] (agent_id)
	agentId, err := state.GetString("id")
	if err != nil {
		stats <- MakeEvent(AgentQueryBadState)
		return nil, err
	}

	// state["frameworks"][0]["id"] (framework_id) => state["frameworks"][0]["name"] (framework_name)
	frameworks, err := state.GetObjectArray("frameworks")
	if err != nil {
		stats <- MakeEvent(AgentQueryBadState)
		return nil, err
	}
	frameworkNames := make(map[string]string, len(frameworks))
	for _, framework := range frameworks {
		frameworkId, err := framework.GetString("id")
		if err != nil {
			stats <- MakeEvent(AgentQueryBadState)
			return nil, err
		}
		frameworkName, err := framework.GetString("name")
		if err != nil {
			stats <- MakeEvent(AgentQueryBadState)
			return nil, err
		}
		frameworkNames[frameworkId] = frameworkName
	}

	return &AgentState{agentId, frameworkNames}, nil
}
