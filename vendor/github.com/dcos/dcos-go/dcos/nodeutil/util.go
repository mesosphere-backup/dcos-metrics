package nodeutil

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"os"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/dcos/dcos-go/dcos"
	"github.com/dcos/dcos-go/exec"
)

const (
	defaultExecTimeout       = 10 * time.Second
	defaultClusterIDLocation = "/var/lib/dcos/cluster-id"
)

// ErrTaskNotFound is return if the canonical ID for a given task not found.
var ErrTaskNotFound = errors.New("task not found")

var defaultStateURL = url.URL{
	Scheme: "http",
	Host:   net.JoinHostPort(dcos.DNSRecordLeader, strconv.Itoa(dcos.PortMesosMaster)),
	Path:   "/state",
}

// The key type is unexported to prevent collisions with context keys defined in
// other packages.
type key int

// requestHeaderKey is a context key for the user get request headers.
var requestHeaderKey key = 1

// ErrNodeInfo is an error structure raised by exported functions with meaningful error message.
type ErrNodeInfo struct {
	msg string
}

func (e ErrNodeInfo) Error() string {
	return e.msg
}

// NodeInfo defines an interface to interact with DC/OS cluster via go methods.
type NodeInfo interface {
	DetectIP() (net.IP, error)
	IsLeader() (bool, error)
	MesosID(context.Context) (string, error)
	ClusterID() (string, error)
	TaskCanonicalID(ctx context.Context, task string, completed bool) (*CanonicalTaskID, error)
}

// CanonicalTaskID is a unique task id.
type CanonicalTaskID struct {
	ID           string
	AgentID      string
	FrameworkID  string
	ExecutorID   string
	ContainerIDs []string
}

// dcosInfo is implementation of NodeInfo interface.
type dcosInfo struct {
	sync.Mutex
	// save cached data
	cache bool

	// cached data
	cachedIP        *net.IP
	cachedIsLeader  *bool
	cachedMesosID   string
	cachedClusterID string

	// caller parameters
	client            *http.Client
	detectIPLocation  string
	detectIPTimeout   time.Duration
	role              string
	mesosStateURL     string
	dnsRecordLeader   string
	clusterIDLocation string
}

func getDefaultShellPath() string {
	switch runtime.GOOS {
	case "windows":
		return "powershell.exe"
	default:
		return "/bin/bash"
	}
}

// NewNodeInfo returns a new instance of NodeInfo implementation.
func NewNodeInfo(client *http.Client, role string, options ...Option) (NodeInfo, error) {
	if client == nil {
		return nil, ErrNodeInfo{"Client paramter cannot be empty"}
	}

	validRole := func() bool {
		for _, validRole := range []string{dcos.RoleMaster, dcos.RoleAgent, dcos.RoleAgentPublic} {
			if role == validRole {
				return true
			}
		}
		return false
	}

	if !validRole() {
		return nil, ErrNodeInfo{
			fmt.Sprintf("Role paramter is invalid or empty. Got %s", role),
		}
	}

	// setup dcosInfo with default parameters.
	d := &dcosInfo{
		client:            client,
		role:              role,
		cache:             true,
		detectIPLocation:  dcos.GetFileDetectIPLocation(),
		detectIPTimeout:   defaultExecTimeout,
		dnsRecordLeader:   dcos.DNSRecordLeader,
		mesosStateURL:     defaultStateURL.String(),
		clusterIDLocation: defaultClusterIDLocation,
	}

	// update parameters with a caller input.
	for _, opt := range options {
		if opt != nil {
			if err := opt(d); err != nil {
				return nil, err
			}
		}
	}

	return d, nil
}

// DetectIP returns an output from `FileDetectIP` script.
// This is a nice way of shelling out to `detect_ip` script which handles timeout.
func (d *dcosInfo) DetectIP() (net.IP, error) {
	// get ip address from cache.
	d.Lock()
	defer d.Unlock()

	// retrieve from cache
	if d.cache && d.cachedIP != nil {
		return *d.cachedIP, nil
	}

	if _, err := os.Stat(d.detectIPLocation); err != nil {
		return nil, err
	}

	ctx, cancel := context.WithTimeout(context.Background(), d.detectIPTimeout)
	defer cancel()
	ce, err := exec.Run(ctx, getDefaultShellPath(), []string{d.detectIPLocation})
	if err != nil {
		return nil, err
	}

	buf, err := ioutil.ReadAll(ce)
	if err != nil {
		return nil, err
	}

	err = <-ce.Done
	if err != nil {
		return nil, err
	}

	// strip the trailing \n
	detectedIP := string(bytes.TrimSpace(buf))
	if detectedIP == "" {
		return nil, ErrNodeInfo{fmt.Sprintf("command %s return empty output", dcos.GetFileDetectIPLocation())}
	}

	validIP := net.ParseIP(detectedIP)
	if validIP == nil {
		return nil, ErrNodeInfo{fmt.Sprintf("command %s returned invalid IP address %s", dcos.GetFileDetectIPLocation(), detectedIP)}
	}

	// save retrieved IP address to cache.
	if d.cache {
		d.cachedIP = &validIP
	}

	return validIP, nil
}

// IsLeader checks if the node is leader.
func (d *dcosInfo) IsLeader() (bool, error) {
	// find role and IP before locking the structure.
	localIP, err := d.DetectIP()
	if err != nil {
		return false, err
	}

	d.Lock()
	defer d.Unlock()

	if d.cache && d.cachedIsLeader != nil {
		return *d.cachedIsLeader, nil
	}

	// agent cannot be leader
	if d.role != dcos.RoleMaster {
		return false, nil
	}

	addrs, err := net.LookupIP(d.dnsRecordLeader)
	if err != nil {
		return false, err
	}

	for _, addr := range addrs {
		if localIP.Equal(addr) {
			isLeader := true

			if d.cache {
				d.cachedIsLeader = &isLeader
			}

			return isLeader, nil
		}
	}

	return false, ErrNodeInfo{fmt.Sprintf("Error getting mesos leader. Number of ip addresses %d", len(addrs))}
}

// MesosID returns a mesosID for leading master and agents.
// This function will panic if dcosInfo is missing http.Client or mesosStateURL is empty.
func (d *dcosInfo) MesosID(ctx context.Context) (string, error) {
	if d.client == nil {
		panic("Unable to get mesos ID. Uninitialized http client")
	}

	if d.mesosStateURL == "" {
		panic("Unable to get mesos ID. Uninitialized url")
	}

	// retrieve from cache
	d.Lock()
	if d.cache && d.cachedMesosID != "" {
		var result = d.cachedMesosID
		d.Unlock()
		return result, nil
	}
	d.Unlock()

	// if we request IP for an agent node, we should look at `slaves` field.
	localIP, err := d.DetectIP()
	if err != nil {
		return "", err
	}

	state, err := d.state(ctx)
	if err != nil {
		return "", err
	}

	// if the request for a master node, give back the top level ID from state.json
	if d.role == dcos.RoleMaster {
		if state.ID == "" {
			return "", ErrNodeInfo{"Unable to retrieve mesos id for master node. ID field is empty"}
		}

		if d.cache {
			d.Lock()
			d.cachedMesosID = state.ID
			d.Unlock()
		}

		return state.ID, nil
	}

	for _, slave := range state.Slaves {

		validSlaveIP, err := getIPFromPIDField(slave.Pid)
		if err != nil {
			return "", err
		}

		if localIP.Equal(*validSlaveIP) {
			if d.cache {
				d.Lock()
				d.cachedMesosID = slave.ID
				d.Unlock()
			}
			return slave.ID, nil
		}
	}

	return "", ErrNodeInfo{fmt.Sprintf("Local node's IP %s not found in mesos state response %+v", localIP, state)}
}

func getIPFromPIDField(s string) (*net.IP, error) {
	errMsg := fmt.Sprintf("Expecting pid in the following format `slave(1)@ip-address:port`. Got %s", s)

	slaveStr := strings.Split(s, "@")
	if len(slaveStr) != 2 {
		return nil, ErrNodeInfo{errMsg}
	}

	ipPortStr := strings.Split(slaveStr[1], ":")
	if len(ipPortStr) != 2 {
		return nil, ErrNodeInfo{errMsg}
	}

	validSlaveIP := net.ParseIP(ipPortStr[0])
	if validSlaveIP == nil {
		return nil, ErrNodeInfo{fmt.Sprintf("Incorrect IP in response %s", ipPortStr[0])}
	}

	return &validSlaveIP, nil
}

// ClusterID returns a UUID of a specific cluster. The file containing the UUID
// is available on every node at d.clusterIDLocation.
func (d *dcosInfo) ClusterID() (string, error) {
	d.Lock()
	defer d.Unlock()

	if d.cache && d.cachedClusterID != "" {
		return d.cachedClusterID, nil
	}

	body, err := ioutil.ReadFile(d.clusterIDLocation)
	if err != nil {
		return "", err
	}

	clusterID := string(bytes.TrimSpace(body))
	if clusterID == "" {
		return "", ErrNodeInfo{"Empty cluster ID"}
	}

	if !validateUUID(clusterID) {
		return "", ErrNodeInfo{fmt.Sprintf("UUID validation failed. ClusterID: %s", clusterID)}
	}

	if d.cache {
		d.cachedClusterID = clusterID
	}

	return clusterID, nil
}

func (d *dcosInfo) state(ctx context.Context) (state State, err error) {
	req, err := http.NewRequest("GET", d.mesosStateURL, nil)
	if err != nil {
		return state, err
	}

	if ctx != nil {
		if header, ok := HeaderFromContext(ctx); ok {
			req.Header = header
		}
		req = req.WithContext(ctx)
	}

	resp, err := d.client.Do(req)
	if err != nil {
		return state, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return state, ErrNodeInfo{fmt.Sprintf("GET request to %s returned response code %d", d.mesosStateURL, resp.StatusCode)}
	}

	err = json.NewDecoder(resp.Body).Decode(&state)
	return state, err
}

func findTask(name string, completed bool, frameworks []Framework) (foundTasks []Task) {
	for _, framework := range frameworks {
		currentTasks := framework.Tasks
		if completed {
			currentTasks = framework.CompletedTasks
		}

		for _, t := range currentTasks {
			if t.Name != name && !strings.Contains(t.ID, name) {
				continue
			}
			foundTasks = append(foundTasks, t)
		}
	}
	return
}

// TaskCanonicalID return a CanonicalTaskID for a given task.
func (d *dcosInfo) TaskCanonicalID(ctx context.Context, task string, completed bool) (*CanonicalTaskID, error) {
	state, err := d.state(ctx)
	if err != nil {
		return nil, err
	}

	frameworksTasks := findTask(task, completed, state.Frameworks)

	var completedFrameworksTasks []Task
	if completed {
		completedFrameworksTasks = findTask(task, completed, state.CompletedFrameworks)
	}

	foundTasks := append(frameworksTasks, completedFrameworksTasks...)

	if len(foundTasks) == 0 {
		return nil, ErrTaskNotFound
	} else if len(foundTasks) > 1 {
		var taskIDs []string
		for _, task := range foundTasks {
			taskIDs = append(taskIDs, task.ID)
		}
		return nil, fmt.Errorf("found more then 1 task with name %s: %s", task, taskIDs)
	}

	t := foundTasks[0]
	containerIDs, err := t.ContainerIDs()
	if err != nil {
		return nil, err
	}

	return &CanonicalTaskID{
		ID:           t.ID,
		AgentID:      t.SlaveID,
		FrameworkID:  t.FrameworkID,
		ExecutorID:   t.ExecutorID,
		ContainerIDs: containerIDs,
	}, nil
}

// HeaderFromContext returns http.Header from a context if it's found.
func HeaderFromContext(ctx context.Context) (http.Header, bool) {
	if ctx == nil {
		panic("Context cannot be nil")
	}

	requestValue := ctx.Value(requestHeaderKey)
	if requestValue == nil {
		return nil, false
	}

	header, ok := requestValue.(http.Header)
	return header, ok
}

// NewContextWithHeaders adds http.Header to the instance of context.
func NewContextWithHeaders(ctx context.Context, header http.Header) context.Context {
	if ctx == nil {
		ctx = context.Background()
	}
	return context.WithValue(ctx, requestHeaderKey, header)
}
