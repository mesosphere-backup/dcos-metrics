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
	"net"
	"net/http"
	"os"
	"sync"
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
	}

	newPlugin.App = cli.NewApp()
	newPlugin.App.Version = version
	newPlugin.App.Flags = []cli.Flag{
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
			if err := p.BeforeFunc(c); err != nil {
				return err
			}
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
			if err := p.AfterFunc(c); err != nil {
				return err
			}
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

// Metrics queries the local dcos-metrics API and returns a slice of
// producers.MetricsMessage.
func (p *Plugin) Metrics() ([]producers.MetricsMessage, error) {
	var messages []producers.MetricsMessage

	// Fetch node metrics
	nodeMetrics, err := p.getNodeMetrics()
	if err != nil {
		return nil, err
	}
	messages = append(messages, nodeMetrics)

	// Master only collects node metrics; return without checking containers
	if p.Role == dcos.RoleMaster {
		return messages, nil
	}

	// Fetch container metrics
	containerMetrics, err := p.getContainerMetrics()
	if err != nil {
		return nil, err
	}
	messages = append(messages, containerMetrics...)
	return messages, nil
}

// getNodeMetrics queries the /node endpoint and returns metrics found there
func (p *Plugin) getNodeMetrics() (producers.MetricsMessage, error) {
	message, err := makeMetricsRequest(p.Client, "http://localhost/v0/node")
	return message, errors.Wrap(err, "could not read node metrics")
}

// getContainerMetrics queries the /containers/<id> and /containers/<id>/app
// endpoint for each container on the machine. It returns a slice of metrics
// messages, one for each hit.
func (p *Plugin) getContainerMetrics() ([]producers.MetricsMessage, error) {
	ids, err := p.getContainerList()
	if err != nil {
		return nil, errors.Wrap(err, "could not read list of containers")
	}

	var futures []<-chan producers.MetricsMessage
	for _, id := range ids {
		url1 := fmt.Sprintf("http://localhost/v0/containers/%s", id)
		url2 := fmt.Sprintf("http://localhost/v0/containers/%s/app", id)
		futures = append(futures, makeMetricsRequestFuture(p.Client, url1), makeMetricsRequestFuture(p.Client, url2))
	}

	var messages []producers.MetricsMessage
	for future := range mergeFutures(futures) {
		messages = append(messages, future)
	}

	return messages, nil
}

// getContainerList queries the /containers endpoint and returns a slice of
// container IDs.
func (p *Plugin) getContainerList() ([]string, error) {
	var ids []string

	resp, err := p.Client.Get("http://localhost/v0/containers")
	if err != nil {
		return nil, err
	}

	defer resp.Body.Close()
	err = json.NewDecoder(resp.Body).Decode(&ids)
	return ids, errors.Wrap(err, "could not decode container list")
}

/*** Helpers ***/

// mergeFutures aggregates multiple channels into a single output channel
func mergeFutures(cs []<-chan producers.MetricsMessage) <-chan producers.MetricsMessage {
	var wg sync.WaitGroup
	out := make(chan producers.MetricsMessage)

	// Start an output goroutine for each input channel in cs.  output
	// copies values from c to out until c is closed, then calls wg.Done.
	output := func(c <-chan producers.MetricsMessage) {
		for n := range c {
			out <- n
		}
		wg.Done()
	}
	wg.Add(len(cs))
	for _, c := range cs {
		go output(c)
	}

	// Start a goroutine to close out once all the output goroutines are
	// done.  This must start after the wg.Add call.
	go func() {
		wg.Wait()
		close(out)
	}()
	return out
}

// makeMetricsRequestFuture wraps makeMetricsRequest in a goroutine, returning
// a channel on which the MetricsMessage will be returned.
func makeMetricsRequestFuture(client *http.Client, url string) <-chan producers.MetricsMessage {
	out := make(chan producers.MetricsMessage, 1)
	go func() {
		message, _ := makeMetricsRequest(client, url)
		out <- message
		close(out)
	}()
	return out
}

// makeMetricsRequest queries the given url expecting to find a JSON-formatted
// MetricsMessage, which it returns.
func makeMetricsRequest(client *http.Client, url string) (producers.MetricsMessage, error) {
	l := logrus.WithFields(logrus.Fields{"plugin": "http-helper"})

	l.Infof("Making request to %s", url)
	mm := producers.MetricsMessage{}

	resp, err := client.Get(url)
	if err != nil {
		l.Errorf("Encountered error requesting data, %s", err.Error())
		return mm, err
	}

	// 204 No Content is not an error code; we handle it explicitly
	if resp.StatusCode == http.StatusNoContent {
		l.Warnf("Empty response received from endpoint: %s", url)
		return mm, nil
	}

	defer resp.Body.Close()
	err = json.NewDecoder(resp.Body).Decode(&mm)
	return mm, errors.Wrapf(err, "could not decode metrics response: %s", url)
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
