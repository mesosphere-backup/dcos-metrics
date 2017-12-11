# dcos-go/dcos/nodeutil
An easy way of interact with DC/OS specific services and variables.

## Overview
dcos-go/dcos/nodeutil provides a golang interface to DC/OS cluster.

- `DetectIP()` executes the `detect_ip` script and validates the result.
- `Role()` returns a node's role in a cluster.
- `IsLeader()` returns true if the host is a leading master.
- `MesosID(*context.Context)` returns a node's mesos ID. Optionally can accept an instance of Context to control
  request cancellation from a caller.
- `ClusterID()` returns a UUID of a cluster.
- `TaskCanonicalID(context.Context, task)` returns a canonical node ID for a given task.
  This includes the mesos agent, framework, executor and container IDs.

Note: Methods `IsLeader()` and `ClusterID()` will only work on master nodes.

## Usage
```go

import (
    "crypto/tls"
    "net/http"

    "github.com/dcos/dcos-go/dcos/nodeutil"
)

func main() {
    client := &http.Client{
        Transport: &http.Transport{
            TLSClientConfig: &tls.Config{
                InsecureSkipVerify: true,
            },
        },
    }
    d, err := nodeutil.NewNodeInfo(client)
    if err != nil {
        panic(err)
    }

    ip, err := d.DetectIP()
    if err != nil {
        panic(err)
    }

    r, err := d.Role()
    if err != nil {
        panic(err)
    }

    leader, err := d.IsLeader()
    if err != nil {
        panic(err)
    }

    mesosID, err := d.MesosID(nil)
    if err != nil {
        panic(err)
    }
}
```
