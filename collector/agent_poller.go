package collector

import (
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"os/exec"
	"strings"
	"time"

	"github.com/antonholmquist/jason"
	"github.com/dcos/dcos-metrics/collector/metrics-schema"
	"github.com/linkedin/goavro"
)

var (
	agentIPCommandFlag = StringEnvFlag("agent-ip-command", "/opt/mesosphere/bin/detect_ip",
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
	containerIDKey = "container_id"
	executorIDKey  = "executor_id"
	frameworkIDKey = "framework_id"
)

// can't be const:
var marathonAppIDLabelKeys = map[string]bool{
	"MARATHON_APP_ID": true,
	"DCOS_SPACE":      true,
}

type AgentState struct {
	// agent_id
	agentID string
	// framework_id => framework_name
	frameworkNames map[string]string
	// executor_id => application_name
	executorAppNames map[string]string
}

// RunAgentPoller runs an Agent Poller which periodically produces data retrieved from a local Mesos Agent.
// This function should be run as a gofunc.
func RunAgentPoller(recordsChan chan<- *AvroDatum, agentStateChan chan<- *AgentState, stats chan<- StatsEvent) {
	// fetch agent ip once. per DC/OS docs, changing a node IP is unsupported
	var agentIP string
	if len(*agentTestStateFileFlag) == 0 ||
		len(*agentTestSystemFileFlag) == 0 ||
		len(*agentTestContainersFileFlag) == 0 {
		// only get the ip if actually needed
		agentIP = getAgentIP(stats)
	}

	// do one poll immediately upon starting, to ensure that agent metadata is populated early:
	pollAgent(agentIP, recordsChan, agentStateChan, stats)
	ticker := time.NewTicker(time.Second * time.Duration(*agentPollPeriodFlag))
	for {
		select {
		case _ = <-ticker.C:
			pollAgent(agentIP, recordsChan, agentStateChan, stats)
		}
	}
}

// ---

func pollAgent(agentIP string, recordsChan chan<- *AvroDatum, agentStateChan chan<- *AgentState, stats chan<- StatsEvent) {
	// always fetch/emit agent state first: downstream will use it for tagging metrics
	agentState, err := getAgentState(agentIP, stats)
	if err == nil {
		agentStateChan <- agentState
	} else {
		log.Printf("Failed to retrieve state from agent at %s: %s", agentIP, err)
	}

	systemMetricsList, err := getSystemMetrics(agentIP, agentState, stats)
	if err == nil {
		if systemMetricsList != nil {
			recordsChan <- systemMetricsList
		}
	} else {
		log.Printf("Failed to retrieve system metrics from agent at %s: %s", agentIP, err)
	}

	containerMetricsLists, err := getContainerMetrics(agentIP, agentState, stats)
	if err == nil {
		for _, metricList := range containerMetricsLists {
			recordsChan <- metricList
		}
	} else {
		log.Printf("Failed to retrieve container metrics from agent at %s: %s", agentIP, err)
	}
}

// getAgentIP runs detect_ip => "10.0.3.26\n"
func getAgentIP(stats chan<- StatsEvent) string {
	stats <- MakeEvent(AgentIPLookup)
	cmdWithArgs := strings.Split(*agentIPCommandFlag, " ")
	ipBytes, err := exec.Command(cmdWithArgs[0], cmdWithArgs[1:]...).Output()
	if err != nil {
		stats <- MakeEvent(AgentIPLookupFailed)
		log.Fatalf("Fetching Agent IP with -agent-ip-command='%s' failed: %s", *agentIPCommandFlag, err)
	}
	ip := strings.TrimSpace(string(ipBytes))
	if len(ip) == 0 {
		stats <- MakeEvent(AgentIPLookupEmpty)
		log.Fatalf("Agent IP fetched with -agent-ip-command='%s' is empty", *agentIPCommandFlag)
	}
	//log.Printf("Agent IP obtained with -agent-ip-command='%s': %s\n", *agentIPCommandFlag, ip)
	return ip
}

// fetches container-level resource metrics from the agent (via /containers), emits to the framework topics (default 'metrics-<framework_id>')
func getContainerMetrics(agentIP string, agentState *AgentState, stats chan<- StatsEvent) ([]*AvroDatum, error) {
	rootJSON, err := getJSONFromAgent(agentIP, "/containers", agentTestContainersFileFlag, stats)
	if err != nil {
		return nil, err
	}

	containersArray, err := rootJSON.ObjectArray()
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	metricLists := make([]*AvroDatum, 0)
	// collect datapoints. expecting a list of dicts, where each dict has:
	// - tags: 'container_id'/'executor_id'/'framework_id' strings
	// - datapoints/timestamp: 'statistics' dict of string=>int/dbl (incl a dbl 'timestamp')
	for _, containerObj := range containersArray {

		// get framework id for topic
		frameworkID, err := containerObj.GetString(frameworkIDKey)
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}

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
					frameworkID, agentState.agentID, err)
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
		// note: agent_id/framework_name tags are automatically added downstream
		tags := make([]interface{}, 0)
		// container_id
		tagVal, err := containerObj.GetString(containerIDKey)
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		tags = addTag(tags, containerIDKey, tagVal)
		// executor_id
		tagVal, err = containerObj.GetString(executorIDKey)
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		tags = addTag(tags, executorIDKey, tagVal)
		// framework_id
		tags = addTag(tags, frameworkIDKey, frameworkID)

		metricListRec, err := goavro.NewRecord(metricListNamespace, metricListSchema)
		if err != nil {
			log.Fatalf("Failed to create MetricList record for topic %s (agent %s): %s",
				frameworkID, agentState.agentID, err)
		}
		metricListRec.Set("topic", frameworkID)
		metricListRec.Set("datapoints", datapoints)
		metricListRec.Set("tags", tags)
		// just use a size of zero, relative to limits it'll be insignificant anyway:
		metricLists = append(metricLists, &AvroDatum{metricListRec, frameworkID, 0})
	}
	return metricLists, nil
}

// fetches system-level metrics from the agent (via /metrics/snapshot), emits to the agent topic (default 'metrics-agent')
func getSystemMetrics(agentIP string, agentState *AgentState, stats chan<- StatsEvent) (*AvroDatum, error) {
	rootJSON, err := getJSONFromAgent(agentIP, "/metrics/snapshot", agentTestSystemFileFlag, stats)
	if err != nil {
		return nil, err
	}

	json, err := rootJSON.Object()
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
				*agentMetricsTopicFlag, agentState.agentID, err)
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
			*agentMetricsTopicFlag, agentState.agentID, err)
	}
	metricListRec.Set("topic", *agentMetricsTopicFlag)
	metricListRec.Set("datapoints", datapoints)
	// note: agent_id tag is automatically added downstream
	metricListRec.Set("tags", make([]interface{}, 0))
	// just use a size of zero, relative to limits it'll be insignificant anyway:
	return &AvroDatum{metricListRec, *agentMetricsTopicFlag, 0}, nil
}

// fetches container state from the agent (via /state) to populate AgentState
func getAgentState(agentIP string, stats chan<- StatsEvent) (*AgentState, error) {
	rootJSON, err := getJSONFromAgent(agentIP, "/state", agentTestStateFileFlag, stats)
	if err != nil {
		return nil, err
	}

	json, err := rootJSON.Object()
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	// state["id"] (agent_id)
	agentID, err := json.GetString("id")
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	frameworks, err := json.GetObjectArray("frameworks")
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	// state["frameworks"][N]["id"] (framework_id)
	// => state["frameworks"][N]["name"] (framework_name)
	frameworkNames := make(map[string]string, len(frameworks))

	// state["frameworks"][N]["executors"][M]["id"] (executor_id)
	// => state["frameworks"][N]["executors"][M]["labels"][L(MARATHON_APP_ID)]["value"] (application_name)
	executorAppNames := make(map[string]string, 0)

	for _, framework := range frameworks {
		frameworkID, err := framework.GetString("id")
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		frameworkName, err := framework.GetString("name")
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		frameworkNames[frameworkID] = frameworkName

		executors, err := framework.GetObjectArray("executors")
		if err != nil {
			stats <- MakeEvent(AgentQueryBadData)
			return nil, err
		}
		for _, executor := range executors {
			executorID, err := executor.GetString("id")
			if err != nil {
				stats <- MakeEvent(AgentQueryBadData)
				return nil, err
			}
			labels, err := executor.GetObjectArray("labels")
			if err != nil {
				// ignore this failure: labels are often missing when it's not a marathon app.
				continue
			}
			// check for marathon app id. if present, store as application name:
			for _, label := range labels {
				labelKey, err := label.GetString("key")
				if err != nil {
					stats <- MakeEvent(AgentQueryBadData)
					return nil, err
				}
				_, ok := marathonAppIDLabelKeys[labelKey]
				if ok {
					labelValue, err := label.GetString("value")
					if err != nil {
						stats <- MakeEvent(AgentQueryBadData)
						return nil, err
					}
					executorAppNames[executorID] = strings.TrimLeft(labelValue, "/")
				}
			}
		}
	}

	return &AgentState{
		agentID:          agentID,
		frameworkNames:   frameworkNames,
		executorAppNames: executorAppNames}, nil
}

func getJSONFromAgent(agentIP string, urlPath string, testFileFlag *string, stats chan<- StatsEvent) (*jason.Value, error) {
	stats <- MakeEvent(AgentQuery)
	var rawJSON []byte
	var err error
	if len(*testFileFlag) == 0 {
		endpoint := fmt.Sprintf("http://%s:%d%s", agentIP, *agentPortFlag, urlPath)
		if len(*authCredentialFlag) == 0 {
			rawJSON, err = HTTPGet(endpoint)
		} else {
			rawJSON, err = AuthedHTTPGet(endpoint, *authCredentialFlag)
		}
		// Special case: on HTTP 401 Unauthorized, exit immediately rather than failing forever
		if httpErr, ok := err.(HTTPCodeError); ok {
			if httpErr.Code == 401 {
				stats <- MakeEvent(AgentQueryFailed)
				log.Fatalf("Got 401 Unauthorized when querying agent. "+
					"Please provide a suitable auth token using the AUTH_CREDENTIAL env var: %s", err)
			}
		}
	} else {
		rawJSON, err = ioutil.ReadFile(*testFileFlag)
	}
	if err != nil {
		stats <- MakeEvent(AgentQueryFailed)
		return nil, err
	}

	json, err := jason.NewValueFromBytes(rawJSON)
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}
	return json, nil
}
