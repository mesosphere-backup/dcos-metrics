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

package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"strings"
	"time"

	"github.com/dcos/dcos-go/dcos"
	"github.com/dcos/dcos-go/dcos/nodeutil"
	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/collectors/framework"
	mesosAgent "github.com/dcos/dcos-metrics/collectors/mesos/agent"
	"github.com/dcos/dcos-metrics/collectors/node"
	httpProducer "github.com/dcos/dcos-metrics/producers/http"
	httpClient "github.com/dcos/dcos-metrics/util/http/client"
	httpHelpers "github.com/dcos/dcos-metrics/util/http/helpers"

	log "github.com/Sirupsen/logrus"
	yaml "gopkg.in/yaml.v2"
)

var (
	// VERSION set by $(git describe --always)
	// Set by scripts/build.sh, executed by `make build`
	VERSION = "unset"
	// REVISION set by $(git rev-parse --shore HEAD)
	// Set by scripts/build.sh, executed by `make build`
	REVISION = "unset"
)

// Config defines the top-level configuration options for the dcos-metrics-collector project.
// It is (currently) broken up into two main sections: collectors and producers.
type Config struct {
	// Config from the service config file
	Collector         CollectorConfig `yaml:"collector"`
	Producers         ProducersConfig `yaml:"producers"`
	IAMConfigPath     string          `yaml:"iam_config_path"`
	CACertificatePath string          `yaml:"ca_certificate_path"`
	// Node info
	nodeInfo collectors.NodeInfo

	// Flag configuration
	DCOSRole    string
	ConfigPath  string
	LogLevel    string
	VersionFlag bool
}

// CollectorConfig contains configuration options relevant to the "collector"
// portion of this project. That is, the code responsible for querying Mesos,
// et. al to gather metrics and send them to a "producer".
type CollectorConfig struct {
	HTTPProfiler bool                  `yaml:"http_profiler"`
	Framework    *framework.Collector  `yaml:"framework,omitempty"`
	Node         *node.Collector       `yaml:"node,omitempty"`
	MesosAgent   *mesosAgent.Collector `yaml:"mesos_agent,omitempty"`
}

// ProducersConfig contains references to other structs that provide individual producer configs.
// The configuration for all producers is then located in their corresponding packages.
//
// For example: Config.Producers.KafkaProducerConfig references kafkaProducer.Config. This struct
// contains an optional Kafka configuration. This configuration is available in the source file
// 'producers/kafka/kafka.go'. It is then the responsibility of the individual producers to
// validate the configuration the user has provided and panic if necessary.
type ProducersConfig struct {
	HTTPProducerConfig httpProducer.Config `yaml:"http,omitempty"`
	//KafkaProducerConfig  kafkaProducer.Config  `yaml:"kafka,omitempty"`
	//StatsdProducerConfig statsdProducer.Config `yaml:"statsd,omitempty"`
}

func (c *Config) setFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.ConfigPath, "config", c.ConfigPath, "The path to the config file.")
	fs.StringVar(&c.LogLevel, "loglevel", c.LogLevel, "Logging level (default: info). Must be one of: debug, info, warn, error, fatal, panic.")
	fs.StringVar(&c.DCOSRole, "role", c.DCOSRole, "The DC/OS role this instance runs on.")
	fs.BoolVar(&c.VersionFlag, "version", c.VersionFlag, "Print version and revsion then exit")
}

func (c *Config) loadConfig() error {
	fileByte, err := ioutil.ReadFile(c.ConfigPath)
	if err != nil {
		return err
	}

	if err = yaml.Unmarshal(fileByte, &c); err != nil {
		return err
	}

	return nil
}

func (c *Config) getNodeInfo() error {
	log.Debug("Getting node info")
	client, err := httpHelpers.NewMetricsClient(c.CACertificatePath, c.IAMConfigPath)
	if err != nil {
		return err
	}

	// Create a new DC/OS nodeutil instance
	var stateURL = "http://leader.mesos:5050/state"
	if len(c.IAMConfigPath) > 0 {
		stateURL = "https://leader.mesos:5050/state"
	}
	if len(c.Collector.MesosAgent.Principal) > 0 {
		stateURL = "http://" + c.Collector.MesosAgent.Principal + ":" + c.Collector.MesosAgent.Secret + "@leader.mesos:5050/state"
	}
	node, err := nodeutil.NewNodeInfo(client, c.DCOSRole, nodeutil.OptionMesosStateURL(stateURL))
	if err != nil {
		return fmt.Errorf("error: could not get nodeInfo: %s", err)
	}

	// Get node IP address
	ip, err := node.DetectIP()
	if err != nil {
		return fmt.Errorf("error: could not detect node IP: %s", err)
	}
	c.nodeInfo.IPAddress = ip.String()
	c.nodeInfo.Hostname = c.nodeInfo.IPAddress // TODO(roger): need hostname support in nodeutil

	// Get Mesos master/agent ID
	mid, err := node.MesosID(nil)
	if err != nil {
		return fmt.Errorf("error: could not get Mesos node ID: %s", err)
	}
	c.nodeInfo.MesosID = mid

	// Get cluster ID
	cid, err := node.ClusterID()
	if err != nil {
		return err
	}
	c.nodeInfo.ClusterID = cid

	// Leader
	c.nodeInfo.Leader = "leader.mesos:5050"

	return nil
}

// newConfig establishes our default, base configuration.
func newConfig() Config {
	return Config{
		Collector: CollectorConfig{
			HTTPProfiler: false,
			Framework: &framework.Collector{
				ListenEndpointFlag:         "127.0.0.1:8124",
				RecordInputLogFlag:         false,
				InputLimitAmountKBytesFlag: 20480,
				InputLimitPeriodFlag:       60,
			},
			MesosAgent: &mesosAgent.Collector{
				PollPeriod:      time.Duration(60 * time.Second),
				Port:            5051,
				RequestProtocol: "http",
			},
			Node: &node.Collector{
				PollPeriod: time.Duration(60 * time.Second),
			},
		},
		Producers: ProducersConfig{
			HTTPProducerConfig: httpProducer.Config{
				CacheExpiry: time.Duration(120 * time.Second),
				Port:        9000,
			},
		},
		LogLevel: "info",
	}
}

// getNewConfig loads the configuration and sets precedence of configuration values.
// For example: command line flags override values provided in the config file.
func getNewConfig(args []string) (Config, error) {
	c := newConfig()
	thisFlagSet := flag.NewFlagSet("", flag.ExitOnError)
	c.setFlags(thisFlagSet)

	// Override default config with CLI flags if any
	if err := thisFlagSet.Parse(args); err != nil {
		fmt.Println("Errors encountered parsing flags.")
		return c, err
	}

	// If the -version flag was passed, ignore all other args, print the version, and exit
	if c.VersionFlag {
		fmt.Printf(strings.Join([]string{
			fmt.Sprintf("DC/OS Metrics Service (%s)", c.DCOSRole),
			fmt.Sprintf("Version: %s", VERSION),
			fmt.Sprintf("Revision: %s", REVISION),
			fmt.Sprintf("HTTP User-Agent: %s", httpClient.USERAGENT),
		}, "\n"))
		os.Exit(0)
	}
	log.Info("Configpath: ", c.ConfigPath)
	if len(c.ConfigPath) > 0 {
		if err := c.loadConfig(); err != nil {
			return c, err
		}
	} else {
		log.Warnf("No config file specified, using all defaults.")
	}

	if len(strings.Split(c.DCOSRole, " ")) != 1 {
		return c, fmt.Errorf("error: must specify exactly one DC/OS role (master or agent)")
	}

	if c.DCOSRole != dcos.RoleMaster && c.DCOSRole != dcos.RoleAgent {
		return c, fmt.Errorf("error: expected role to be 'master' or 'agent, got: %s", c.DCOSRole)
	}

	// Note: .getNodeInfo() is last so we are sure we have all the
	// configuration we need from flags and config file to make
	// this run correctly.
	if err := c.getNodeInfo(); err != nil {
		return c, err
	}

	// Set the client for the collector to reuse in GET operations
	// to local state and other HTTP sessions
	collectorClient, err := httpHelpers.NewMetricsClient(c.CACertificatePath, c.IAMConfigPath)
	if err != nil {
		return c, err
	}

	c.Collector.MesosAgent.HTTPClient = collectorClient

	return c, nil
}
