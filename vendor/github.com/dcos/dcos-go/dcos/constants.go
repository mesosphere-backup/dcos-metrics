package dcos

// DC/OS roles.
const (
	// RoleMaster defines a master role.
	RoleMaster = "master"

	// RoleAgent defines an agent role.
	RoleAgent = "agent"

	// RoleAgentPublic defines a public agent role.
	RoleAgentPublic = "agent_public"
)

// DC/OS files.
const (
	// FileDetectIP is a shell script on every DC/OS node which provides IP address used by mesos.
	FileDetectIP = "/opt/mesosphere/bin/detect_ip"

	// FileMaster defines a master role in a cluster.
	FileMaster = "/etc/mesosphere/roles/master"

	// FileAgent defines an agent role in a cluster.
	FileAgent = "/etc/mesosphere/roles/slave"

	// FileAgentPublic defines a public agent role in a cluster.
	FileAgentPublic = "/etc/mesosphere/roles/slave_public"
)

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
