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
	"encoding/json"
	"errors"
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"os"
	"strconv"
	"time"

	"github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/dcos"
	"github.com/dcos/dcos-metrics/producers"
	httpHelpers "github.com/dcos/dcos-metrics/util/http/helpers"
	"github.com/urfave/cli"

	yaml "gopkg.in/yaml.v2"
)

// Plugin is used to collect metrics and then send them to a remote system
// (e.g. DataDog, Librato, etc.).  Use plugin.New(...) to build a new plugin.
type Plugin struct {
	App               *cli.App
	Name              string
	Endpoints         []string
	Role              string
	PollingInterval   int
	MetricsPort       int
	MetricsScheme     string
	MetricsHost       string
	Log               *logrus.Entry
	ConnectorFunc     func([]producers.MetricsMessage, *cli.Context) error
	Client            *http.Client
	ConfigPath        string
	IAMConfigPath     string `yaml:"iam_config_path"`
	CACertificatePath string `yaml:"ca_certificate_path"`
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
		cli.StringFlag{
			Name:        "config",
			Value:       newPlugin.ConfigPath,
			Usage:       "The path to the config file.",
			Destination: &newPlugin.ConfigPath,
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

// Metrics polls the DC/OS components and returns a slice of
// producers.MetricsMessage.
func (p *Plugin) Metrics() ([]producers.MetricsMessage, error) {

	metricsMessages := []producers.MetricsMessage{}

	// The first time Metrics() is called, the plugin client
	// should be initialised
	if p.Client == nil {
		if err := p.loadConfig(); err != nil {
			return metricsMessages, err
		}
		if err := p.createClient(); err != nil {
			return metricsMessages, err
		}
	}

	p.Log.Info("Getting metrics from metrics service")
	if err := p.setEndpoints(); err != nil {
		p.Log.Fatal(err)
	}

	for _, path := range p.Endpoints {
		metricsURL := url.URL{
			Scheme: p.MetricsScheme,
			Host:   net.JoinHostPort(p.MetricsHost, strconv.Itoa(p.MetricsPort)),
			Path:   path,
		}

		request := &http.Request{
			Method: "GET",
			URL:    &metricsURL,
		}

		metricMessage, err := makeMetricsRequest(p.Client, request)
		if err != nil {
			return metricsMessages, err
		}

		metricsMessages = append(metricsMessages, metricMessage)
		p.Log.Infof("Received data from metrics service endpoint %s, success!", request.URL.Path)
	}

	return metricsMessages, nil
}

// SetEndpoints uses the role passed as a flag to generate the metrics endpoints
// this instance should use.
func (p *Plugin) setEndpoints() error {
	p.Log.Infof("Setting plugin endpoints for role %s", p.Role)
	if p.Role == dcos.RoleMaster {
		p.Endpoints = []string{
			"/system/v1/metrics/v0/node",
		}
		return nil
	}

	if p.Role == dcos.RoleAgent || p.Role == dcos.RoleAgentPublic {
		p.Endpoints = []string{
			"/system/v1/metrics/v0/node",
		}

		containers := []string{}
		metricsURL := url.URL{
			Scheme: p.MetricsScheme,
			Host:   net.JoinHostPort(p.MetricsHost, strconv.Itoa(p.MetricsPort)),
			Path:   "/system/v1/metrics/v0/containers",
		}

		request := &http.Request{
			Method: "GET",
			URL:    &metricsURL,
		}

		resp, err := p.Client.Do(request)
		if err != nil {
			return err
		}
		defer resp.Body.Close()

		body, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			p.Log.Errorf("Encountered error reading response body, %s", err.Error())
			return err
		}

		if err := json.Unmarshal(body, &containers); err != nil {
			return err
		}

		for _, c := range containers {
			e := "/system/v1/metrics/v0/containers/" + c
			p.Log.Infof("Discovered new container endpoint %s", e)
			p.Endpoints = append(p.Endpoints, e, e+"/app")
		}

		return nil
	}

	return errors.New("Role must be either 'master' or 'agent'")
}

/*** Helpers ***/
func makeMetricsRequest(client *http.Client, request *http.Request) (producers.MetricsMessage, error) {
	l := logrus.WithFields(logrus.Fields{"plugin": "http-helper"})

	l.Infof("Making request to %+v", request.URL)
	mm := producers.MetricsMessage{}

	resp, err := client.Do(request)
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

	err = json.Unmarshal(body, &mm)
	if err != nil {
		l.Errorf("Encountered error parsing JSON, %s. JSON Content was: %s", err.Error(), body)
		return mm, err
	}

	return mm, nil
}

// loadConfig loads the CACertPath and IAMConfig from the specified yaml file
// into the corresponding Plugin struct fields
func (p *Plugin) loadConfig() error {
	// ConfigPath is optional; don't attempt to read it if not supplied
	if p.ConfigPath == "" {
		p.Log.Info("No --config flag was supplied; metrics requests will not be authenticated and may fail")
		return nil
	}
	p.Log.Info("Loading optional authentication configuration")
	fileByte, err := ioutil.ReadFile(p.ConfigPath)
	if err != nil {
		return err
	}

	if err = yaml.Unmarshal(fileByte, p); err != nil {
		return err
	}

	return nil
}

// createClient creates an HTTP Client with credentials (if available) and
// attaches it to the plugin
func (p *Plugin) createClient() error {
	p.Log.Info("Creating an HTTP client to poll the local metrics API")
	client, err := httpHelpers.NewMetricsClient(p.CACertificatePath, p.IAMConfigPath)
	if err != nil {
		return err
	}

	p.Client = client
	return nil
}
