// Copyright 2016 Mesosphere, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package plugin

import (
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"os"
	"time"

	"github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/dcos"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/pkg/errors"
	"github.com/urfave/cli"
)

// Plugin is used to collect metrics and then send them to a remote system
// (e.g. DataDog, Librato, etc.).  Use plugin.New(...) to build a new plugin.
type Plugin struct {
	App             *cli.App
	Name            string
	Endpoints       []string
	Role            string
	PollingInterval int
	MetricsPort     int
	MetricsScheme   string
	MetricsHost     string
	Log             *logrus.Entry
	ConnectorFunc   func([]producers.MetricsMessage, *cli.Context) error
	BeforeFunc      func(*cli.Context) error
	AfterFunc       func(*cli.Context) error
	Client          *http.Client
}

var version = "UNSET"

// New returns a mandatory plugin config which every plugin for
// metrics will need
func New(options ...Option) (*Plugin, error) {
	newPlugin := &Plugin{
		Name:            "default",
		PollingInterval: 10,
		MetricsScheme:   "http",
		MetricsHost:     "localhost",
		MetricsPort:     61001,
	}

	newPlugin.App = cli.NewApp()
	newPlugin.App.Version = version
	newPlugin.App.Flags = []cli.Flag{
		cli.StringFlag{
			Name:        "metrics-host",
			Value:       newPlugin.MetricsHost,
			Usage:       "The IP or hostname where DC/OS metrics is running",
			Destination: &newPlugin.MetricsHost,
		},
		cli.StringFlag{
			Name:        "metrics-proto",
			Value:       newPlugin.MetricsScheme,
			Usage:       "The HTTP protocol for the DC/OS metrics service",
			Destination: &newPlugin.MetricsScheme,
		},
		cli.IntFlag{
			Name:        "metrics-port",
			Value:       newPlugin.MetricsPort,
			Usage:       "Port the DC/OS metrics service is running.Defaults to agent adminrouter port",
			Destination: &newPlugin.MetricsPort,
		},
		cli.IntFlag{
			Name:        "polling-interval",
			Value:       newPlugin.PollingInterval,
			Usage:       "Polling interval for metrics in seconds",
			Destination: &newPlugin.PollingInterval,
		},

		cli.StringFlag{
			Name:        "dcos-role",
			Value:       newPlugin.Role,
			Usage:       "DC/OS role, either master or agent",
			Destination: &newPlugin.Role,
		},
	}

	for _, o := range options {
		if err := o(newPlugin); err != nil {
			return newPlugin, err
		}
	}

	newPlugin.Log = logrus.WithFields(logrus.Fields{"plugin": newPlugin.Name})

	return newPlugin, nil
}

// StartPlugin starts a (previously configured) Plugin. It will periodically
// poll the system for metrics and send them to the ConnectorFunc.  This method
// will block.
func (p *Plugin) StartPlugin() error {
	p.App.Before = func(c *cli.Context) error {
		if p.BeforeFunc != nil {
			return p.BeforeFunc(c)
		}
		if p.Role == dcos.RoleMaster || p.Role == dcos.RoleAgent || p.Role == dcos.RoleAgentPublic {
			return p.createClient()
		}
		return fmt.Errorf(
			"--dcos-role %q was not recognized (valid roles: %s, %s, %s)",
			p.Role, dcos.RoleMaster, dcos.RoleAgent, dcos.RoleAgentPublic)
	}
	p.App.After = func(c *cli.Context) error {
		if p.AfterFunc != nil {
			return p.BeforeFunc(c)
		}
		return nil
	}
	p.App.Action = func(c *cli.Context) error {
		for {
			metrics, err := p.Metrics()
			if err != nil {
				return err
			}

			if err := p.ConnectorFunc(metrics, c); err != nil {
				return err
			}

			p.Log.Infof("Polling complete, sleeping for %d seconds", p.PollingInterval)
			time.Sleep(time.Duration(p.PollingInterval) * time.Second)
		}
	}

	return p.App.Run(os.Args)
}

// Metrics polls the local dcos-metrics API and returns a slice of
// producers.MetricsMessage.
func (p *Plugin) Metrics() ([]producers.MetricsMessage, error) {
	var messages []producers.MetricsMessage

	// Fetch node metrics
	nodeMetrics, err := p.getNodeMetrics()
	if err != nil {
		return messages, err
	}
	messages = append(messages, nodeMetrics)

	// Master only collects node metrics; return without checking containers
	if p.Role == dcos.RoleMaster {
		return messages, nil
	}

	// Fetch container metrics
	containerMetrics, err := p.getContainerMetrics()
	if err != nil {
		return messages, err
	}
	messages = append(messages, containerMetrics...)
	return messages, nil
}

// getNodeMetrics polls the /node endpoint and returns metrics found there
func (p *Plugin) getNodeMetrics() (producers.MetricsMessage, error) {
	nodeMetrics, err := makeMetricsRequest(p.Client, "http://localhost/v0/node")
	if err != nil {
		return nodeMetrics, errors.Wrap(err, "could not read node metrics")
	}
	return nodeMetrics, nil
}

// getContainerMetrics polls the /containers/<id> and /containers/<id>/app
// endpoint for each container on the machine. It returns a slice of metrics
// messages, one for each hit.
func (p *Plugin) getContainerMetrics() ([]producers.MetricsMessage, error) {
	var messages []producers.MetricsMessage
	ids, err := p.getContainerList()
	if err != nil {
		return messages, errors.Wrap(err, "could not read list of containers")
	}
	for _, id := range ids {
		containerMetrics, err := makeMetricsRequest(p.Client, fmt.Sprintf("http://localhost/v0/containers/%s", id))
		if err != nil {
			return messages, errors.Wrapf(err, "could not retrieve metrics for container %s", id)
		}
		containerAppMetrics, err := makeMetricsRequest(p.Client, fmt.Sprintf("http://localhost/v0/containers/%s/app", id))
		if err != nil {
			return messages, errors.Wrapf(err, "could not retrieve app metrics for container %s", id)
		}
		messages = append(messages, containerMetrics, containerAppMetrics)
	}
	return messages, nil
}

// getContainerList polls the /containers endpoint and returns a slice of
// container IDs.
func (p *Plugin) getContainerList() ([]string, error) {
	var ids []string

	resp, err := p.Client.Get("http://localhost/v0/containers")
	if err != nil {
		return ids, err
	}
	defer resp.Body.Close()

	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		p.Log.Errorf("Encountered error reading response body, %s", err.Error())
		return ids, err
	}
	if err := json.Unmarshal(body, &ids); err != nil {
		return ids, err
	}

	return ids, nil
}

/*** Helpers ***/

// makeMetricsRequest polls the given url expecting to find a JSON-formatted
// MetricsMessage, which it returns.
func makeMetricsRequest(client *http.Client, url string) (producers.MetricsMessage, error) {
	l := logrus.WithFields(logrus.Fields{"plugin": "http-helper"})

	l.Infof("Making request to %+v", url)
	mm := producers.MetricsMessage{}

	resp, err := client.Get(url)
	if err != nil {
		l.Errorf("Encountered error requesting data, %s", err.Error())
		return mm, err
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		l.Errorf("Encountered error reading response body, %s", err.Error())
		return mm, err
	}

	// 204 No Content is not an error code; we handle it explicitly
	if resp.StatusCode == http.StatusNoContent {
		l.Warnf("Empty response received from endpoint: %+v", url)
		return mm, nil
	}

	err = json.Unmarshal(body, &mm)
	if err != nil {
		l.Errorf("Encountered error parsing JSON, %s. JSON Content was: %s", err.Error(), body)
		return mm, err
	}

	return mm, nil
}

// createClient creates an HTTP Client which uses the unix file socket
// appropriate to the plugin's role
func (p *Plugin) createClient() error {
	address := "/run/dcos/dcos-metrics-agent.sock"
	if p.Role == dcos.RoleMaster {
		address = "/run/dcos/dcos-metrics-master.sock"
	}

	p.Log.Infof("Creating metrics API client via %s", address)

	p.Client = &http.Client{
		Transport: &http.Transport{
			DialContext: func(_ context.Context, _, _ string) (net.Conn, error) {
				return net.Dial("unix", address)
			},
		},
	}
	return nil
}
