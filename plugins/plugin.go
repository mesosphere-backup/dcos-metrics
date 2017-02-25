package plugin

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"net/url"

	"github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/dcos"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/urfave/cli"
)

type Plugin struct {
	App             *cli.App
	Endpoints       []string
	Role            string
	PollingInterval int
	MetricsPort     string
	MetricsProto    string
	MetricsHost     string
	AuthToken       string
}

var VERSION = "UNSET"

// New returns a mandatory plugin config which every plugin for
// metrics will need
func New(extFlags []cli.Flag) (*Plugin, error) {
	newPlugin := &Plugin{
		Role:            "",
		PollingInterval: 10,
		MetricsProto:    "http",
		MetricsHost:     "localhost",
		MetricsPort:     "61001",
		AuthToken:       "",
	}

	newPlugin.App = cli.NewApp()
	newPlugin.App.Version = VERSION
	newPlugin.App.Flags = []cli.Flag{
		cli.StringFlag{
			Name:        "metrics-host",
			Value:       newPlugin.MetricsHost,
			Usage:       "The IP or hostname where DC/OS metrics is running",
			Destination: &newPlugin.MetricsHost,
		},
		cli.StringFlag{
			Name:        "metrics-proto",
			Value:       newPlugin.MetricsProto,
			Usage:       "The HTTP protocol for the DC/OS metrics service",
			Destination: &newPlugin.MetricsProto,
		},
		cli.StringFlag{
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
			Name:        "auth-token",
			Value:       newPlugin.AuthToken,
			Usage:       "Valid authentication token for DC/OS services",
			Destination: &newPlugin.AuthToken,
		},
		cli.StringFlag{
			Name:        "dcos-role",
			Value:       newPlugin.Role,
			Usage:       "DC/OS role, either master or agent",
			Destination: &newPlugin.Role,
		},
	}

	for _, f := range extFlags {
		newPlugin.App.Flags = append(newPlugin.App.Flags, f)
	}

	return newPlugin, nil
}

func (p *Plugin) Metrics() ([]producers.MetricsMessage, error) {
	logrus.Info("Getting metrics from metrics service")
	metricsMessages := []producers.MetricsMessage{}

	if err := p.setEndpoints(); err != nil {
		log.Fatal(err)
	}
	for _, path := range p.Endpoints {
		metricsURL := url.URL{
			Scheme: p.MetricsProto,
			Host:   net.JoinHostPort(p.MetricsHost, p.MetricsPort),
			Path:   path,
		}

		if len(p.AuthToken) == 0 {
			return metricsMessages, errors.New("Auth token must be set, use --auth-token <token>")
		}

		request := &http.Request{
			Method: "GET",
			URL:    &metricsURL,
			Header: http.Header{
				"Authorization": []string{fmt.Sprintf("token=%s", p.AuthToken)},
			},
		}

		metricMessage, err := makeMetricsRequest(request)
		if err != nil {
			return metricsMessages, err
		}

		metricsMessages = append(metricsMessages, metricMessage)
		logrus.Infof("Received data from metrics service endpoint %s, success!", request.URL.Path)
	}

	return metricsMessages, nil
}

// SetEndpoints uses the role passed as a flag to generate the metrics endpoints
// this instance should use.
func (p *Plugin) setEndpoints() error {
	logrus.Infof("Setting plugin endpoints for role %s", p.Role)
	if p.Role == dcos.RoleMaster {
		p.Endpoints = []string{
			"/system/v1/metrics/v0/node",
		}
		return nil
	}

	if p.Role == dcos.RoleAgent || p.Role == dcos.RoleAgentPublic {
		p.Endpoints = []string{
			"/system/v1/metrics/v0/node",
			"/system/v1/metrics/v0/containers",
		}
		return nil
	}

	return errors.New("Role must be either 'master' or 'agent'")
}
func makeMetricsRequest(request *http.Request) (producers.MetricsMessage, error) {
	logrus.Infof("Making request to %+v", request.URL)
	client := &http.Client{}
	mm := producers.MetricsMessage{}

	resp, err := client.Do(request)
	if err != nil {
		logrus.Errorf("Encountered error requesting data, %s", err.Error())
		return mm, err
	}

	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		logrus.Errorf("Encountered error reading response body, %s", err.Error())
		return mm, err
	}

	err = json.Unmarshal(body, &mm)
	if err != nil {
		logrus.Errorf("Encountered error parsing JSON, %s", err.Error())
		return mm, err
	}

	return mm, nil
}
