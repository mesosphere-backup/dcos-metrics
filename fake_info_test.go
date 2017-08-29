package main

import (
	"context"
	"net"

	"github.com/dcos/dcos-go/dcos/nodeutil"
)

type fakeInfo struct {
	ip           net.IP
	ipErr        error
	leader       bool
	leaderErr    error
	mesosID      string
	mesosIDErr   error
	clusterID    string
	clusterIDErr error
}

var _ nodeutil.NodeInfo = &fakeInfo{}

func (f *fakeInfo) DetectIP() (net.IP, error) {
	return f.ip, f.ipErr
}

func (f *fakeInfo) IsLeader() (bool, error) {
	return f.leader, f.leaderErr
}

func (f *fakeInfo) MesosID(ctx context.Context) (string, error) {
	return f.mesosID, f.mesosIDErr
}

func (f *fakeInfo) ClusterID() (string, error) {
	return f.clusterID, f.clusterIDErr
}
