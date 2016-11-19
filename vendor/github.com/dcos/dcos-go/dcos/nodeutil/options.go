package nodeutil

import (
	"errors"
	"os"
	"time"
)

// ErrEmptyParam is the error returned if option is used with empty parameter.
var ErrEmptyParam = errors.New("Error using empty parameter")

// Option is a functional option that configures a Reader.
type Option func(*dcosInfo) error

// OptionDetectIP sets the location of detectIP script.
func OptionDetectIP(path string) Option {
	return func(d *dcosInfo) error {
		if _, err := os.Stat(path); err != nil {
			return err
		}

		d.detectIPLocation = path
		return nil
	}
}

// OptionDetectIPTimeout update timeout for detect_ip command.
func OptionDetectIPTimeout(timeout time.Duration) Option {
	return func(d *dcosInfo) error {
		if timeout <= 0 {
			return ErrEmptyParam
		}

		d.detectIPTimeout = timeout
		return nil
	}
}

// OptionMasterRoleFile sets a file on a filesystem which describes that a node is a master.
func OptionMasterRoleFile(path string) Option {
	return func(d *dcosInfo) error {
		if path == "" {
			return ErrEmptyParam
		}
		d.roleMasterFile = path
		return nil
	}
}

// OptionAgentRoleFile sets a file on a filesystem which describes that a node is an agent.
func OptionAgentRoleFile(path string) Option {
	return func(d *dcosInfo) error {
		if path == "" {
			return ErrEmptyParam
		}
		d.roleAgentFile = path
		return nil
	}
}

// OptionAgentPublicRoleFile sets a file on a filesystem which describes that a node is a public agent.
func OptionAgentPublicRoleFile(path string) Option {
	return func(d *dcosInfo) error {
		if path == "" {
			return ErrEmptyParam
		}
		d.roleAgentPublicFile = path
		return nil
	}
}

// OptionMesosStateURL sets a domain name to make a get request to /mesos/state in order to retrieve mesos state.json.
func OptionMesosStateURL(stateURL string) Option {
	return func(d *dcosInfo) error {
		if stateURL == "" {
			return ErrEmptyParam
		}
		d.mesosStateURL = stateURL
		return nil
	}
}

// OptionNoCache disables cache results.
func OptionNoCache() Option {
	return func(d *dcosInfo) error {
		d.cache = false
		return nil
	}
}

// OptionLeaderDNSRecord sets a mesos leader dns entry.
func OptionLeaderDNSRecord(r string) Option {
	return func(d *dcosInfo) error {
		if r == "" {
			return ErrEmptyParam
		}
		d.dnsRecordLeader = r
		return nil
	}
}

// OptionClusterIDFile sets a path to cluster-id file.
func OptionClusterIDFile(f string) Option {
	return func(d *dcosInfo) error {
		if f == "" {
			return ErrEmptyParam
		}
		d.clusterIDLocation = f
		return nil
	}
}
