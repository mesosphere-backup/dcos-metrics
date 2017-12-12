package dcos

import (
	"runtime"
)

// DC/OS roles.
const (
	// RoleMaster defines a master role.
	RoleMaster = "master"

	// RoleAgent defines an agent role.
	RoleAgent = "agent"

	// RoleAgentPublic defines a public agent role.
	RoleAgentPublic = "agent_public"
)

// GetFileDetectIPLocation is a shell script on every DC/OS node which provides IP address used by mesos.
func GetFileDetectIPLocation() string {
	switch runtime.GOOS {
	case "windows":
		return "/mesos/bin/detect_ip.ps1"
	default:
		return "/opt/mesosphere/bin/detect_ip"
	}
}

// DC/OS DNS records.
const (
	// DNSRecordLeader is a domain name used by a leading master in DC/OS cluster.
	DNSRecordLeader = "leader.mesos"
)

// DC/OS ports.
const (
	// PortMesosMaster defines a TCP port for mesos master.
	PortMesosMaster = 5050

	// PortMesosAgent defines a TCP port for mesos agent / public agent.
	PortMesosAgent = 5051
)
