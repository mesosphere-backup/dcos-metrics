package nodeutil

import "errors"

// ErrContainerIDNotFound is returned by ContainerIDs() function if the container id is not set.
var ErrContainerIDNotFound = errors.New("invalid task. Container ID not found")

// State stands for mesos state.json available via /mesos/master/state.json
type State struct {
	ID                  string      `json:"id"`
	Slaves              []Slave     `json:"slaves"`
	Frameworks          []Framework `json:"frameworks"`
	CompletedFrameworks []Framework `json:"completed_frameworks"`
}

// Slave is a field in state.json
type Slave struct {
	ID       string `json:"id"`
	Hostname string `json:"hostname"`
	Port     int    `json:"port"`
	Pid      string `json:"pid"`
}

// Framework is a field in state.json
type Framework struct {
	ID             string `json:"id"`
	Name           string `json:"name"`
	PID            string `json:"pid"`
	Role           string `json:"role"`
	Tasks          []Task `json:"tasks"`
	CompletedTasks []Task `json:"completed_tasks"`
}

// Task is a field in state.json
type Task struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	FrameworkID string `json:"framework_id"`
	ExecutorID  string `json:"executor_id"`
	SlaveID     string `json:"slave_id"`
	State       string `json:"state"`
	Role        string `json:"role"`

	Statuses []Status `json:"statuses"`
}

// Status is a field in state.json
type Status struct {
	ContainerStatus ContainerStatus `json:"container_status"`
}

// ContainerStatus is a field in state.json
type ContainerStatus struct {
	ContainerID NestedValue `json:"container_id"`
}

// NestedValue represents a nested container ID. The value is the actual container ID
// and Parent is a reference to another NestedValue structure.
type NestedValue struct {
	Value  string       `json:"value"`
	Parent *NestedValue `json:"parent"`
}

// ContainerIDs returns a slice of container ids , starting with the current,
// and then appending the parent container ids.
func (t Task) ContainerIDs() (containerIDs []string, err error) {
	for _, status := range t.Statuses {
		containerID := status.ContainerStatus.ContainerID.Value
		if containerID == "" {
			return nil, ErrContainerIDNotFound
		}
		containerIDs = append(containerIDs, containerID)

		parent := status.ContainerStatus.ContainerID.Parent
		for parent != nil {
			containerIDs = append(containerIDs, parent.Value)
			parent = parent.Parent
		}
	}

	if len(containerIDs) == 0 {
		return nil, ErrContainerIDNotFound
	}
	return containerIDs, nil
}
