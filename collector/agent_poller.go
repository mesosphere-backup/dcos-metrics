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

type Agent struct {
	AgentIP        string
	IPCommand      string
	Port           int
	PollPeriod     int
	Topic          string
	AgentStateChan chan<- *AgentState
}

func NewAgent(
	ipCommand string,
	port int,
	pollPeriod int,
	topic string) (Agent, error) {
	a := Agent{}
	if len(ipCommand) == 0 {
		return a, errors.New("Must pass ipAddress to NewAgent()")
	}
	if port < 1024 {
		return a, errors.New("Must pass port to NewAgent()")
	}
	if pollPeriod == 0 {
		return a, errors.New("Must pass pollPeriod to NewAgent()")
	}
	if len(topic) == 0 {
		return a, errors.New("Must pass topic to NewAgent()")
	}

	a.IPCommand = ipCommand
	a.Port = port
	a.PollPeriod = pollPeriod
	a.Topic = topic
	a.AgentStateChan = make(chan *AgentState)

	return a, nil
}

// Run runs an Agent Poller which periodically produces data retrieved from a local Mesos Agent.
// This function should be run as a gofunc.
func (a *Agent) Run(recordsChan chan<- *AvroDatum, stats chan<- StatsEvent) {
	// fetch agent ip once. per DC/OS docs, changing a node IP is unsupported
	if len(*agentTestStateFileFlag) == 0 ||
		len(*agentTestSystemFileFlag) == 0 ||
		len(*agentTestContainersFileFlag) == 0 {
		// only get the ip if actually needed
		//TODO needs err
		a.getIP(stats)
	}

	// do one poll immediately upon starting, to ensure that agent metadata is populated early:
	a.pollAgent(recordsChan, stats)
	ticker := time.NewTicker(time.Second * time.Duration(a.PollPeriod))
	for {
		select {
		case _ = <-ticker.C:
			a.pollAgent(recordsChan, stats)
		}
	}
}

// ---

func (a *Agent) pollAgent(recordsChan chan<- *AvroDatum, stats chan<- StatsEvent) {
	// always fetch/emit agent state first: downstream will use it for tagging metrics
	agentState, err := a.getAgentState(stats)
	if err == nil {
		a.AgentStateChan <- agentState
	} else {
		log.Printf("Failed to retrieve state from agent at %s: %s", a.AgentIP, err)
	}

	systemMetricsList, err := a.getSystemMetrics(agentState, stats)
	if err == nil {
		if systemMetricsList != nil {
			recordsChan <- systemMetricsList
		}
	} else {
		log.Printf("Failed to retrieve system metrics from agent at %s: %s", a.AgentIP, err)
	}

	containerMetricsLists, err := a.getContainerMetrics(agentState, stats)
	if err == nil {
		for _, metricList := range containerMetricsLists {
			recordsChan <- metricList
		}
	} else {
		log.Printf("Failed to retrieve container metrics from agent at %s: %s", a.AgentIP, err)
	}
}

// runs detect_ip => "10.0.3.26\n"
func (a *Agent) getIP(stats chan<- StatsEvent) error {
	stats <- MakeEvent(AgentIPLookup)
	cmdWithArgs := strings.Split(a.IPCommand, " ")
	ipBytes, err := exec.Command(cmdWithArgs[0], cmdWithArgs[1:]...).Output()
	if err != nil {
		stats <- MakeEvent(AgentIPLookupFailed)
		return err
	}
	ip := strings.TrimSpace(string(ipBytes))
	if len(ip) == 0 {
		stats <- MakeEvent(AgentIPLookupEmpty)
		return err
	}

	a.AgentIP = ip

	return nil
}

// fetches container-level resource metrics from the agent (via /containers), emits to the framework topics (default 'metrics-<framework_id>')
func (a *Agent) getContainerMetrics(agentState *AgentState, stats chan<- StatsEvent) ([]*AvroDatum, error) {
	rootJSON, err := a.getJSONFromAgent("/containers", agentTestContainersFileFlag, stats)
	if err != nil {
		return nil, err
	}

	containersArray, err := rootJSON.ObjectArray()
	if err != nil {
		stats <- MakeEvent(AgentQueryBadData)
		return nil, err
	}

	var metricLists []*AvroDatum
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
		var datapoints []interface{}
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
		var tags []interface{}
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
func (a *Agent) getSystemMetrics(agentState *AgentState, stats chan<- StatsEvent) (*AvroDatum, error) {
	rootJSON, err := a.getJSONFromAgent("/metrics/snapshot", agentTestSystemFileFlag, stats)
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
	var datapoints []interface{}
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
				a.Topic, agentState.agentID, err)
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
			a.Topic, agentState.agentID, err)
	}
	metricListRec.Set("topic", a.Topic)
	metricListRec.Set("datapoints", datapoints)
	// note: agent_id tag is automatically added downstream
	metricListRec.Set("tags", make([]interface{}, 0))
	// just use a size of zero, relative to limits it'll be insignificant anyway:
	return &AvroDatum{metricListRec, a.Topic, 0}, nil
}

// fetches container state from the agent (via /state) to populate AgentState
func (a *Agent) getAgentState(stats chan<- StatsEvent) (*AgentState, error) {
	rootJSON, err := a.getJSONFromAgent("/state", agentTestStateFileFlag, stats)
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

func (a *Agent) getJSONFromAgent(urlPath string, testFileFlag *string, stats chan<- StatsEvent) (*jason.Value, error) {
	stats <- MakeEvent(AgentQuery)
	var rawJSON []byte
	var err error
	if len(*testFileFlag) == 0 {
		endpoint := fmt.Sprintf("http://%s:%d%s", a.AgentIP, a.Port, urlPath)
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
