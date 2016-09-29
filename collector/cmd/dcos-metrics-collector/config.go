package main

import (
	"flag"
	"fmt"
	"io/ioutil"

	yaml "gopkg.in/yaml.v2"
)

type MasterConfig struct {
	Port  int    `yaml:"port,omitempty"`
	Topic string `yaml:"metric_topic,omitempty"`
}

type AgentConfig struct {
	Port  int    `yaml:"port,omitempty"`
	Topic string `yaml:"metric_topic,omitempty"`
}

type CollectorConfig struct {
	HttpProfiler  bool   `yaml:"http_profiler"`
	KafkaProducer bool   `yaml:"kafka_producer"`
	IpCommand     string `yaml:"ip_command"`
	PollingPeriod int    `yaml:"polling_period"`

	AgentConfig  AgentConfig  `yaml:"agent_config,omitempty"`
	MasterConfig MasterConfig `yaml:"master_config,omitempty"`

	ConfigPath string
	DCOSRole   string
}

func (c *CollectorConfig) setFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.ConfigPath, "config", c.ConfigPath, "The path to the config file.")
	fs.StringVar(&c.DCOSRole, "role", c.DCOSRole, "The DC/OS role this instance runs on.")
}

func (c *CollectorConfig) loadConfig() error {
	fmt.Printf("Loading config file from %s\n", c.ConfigPath)
	fileByte, err := ioutil.ReadFile(c.ConfigPath)
	if err != nil {
		return err
	}

	if err = yaml.Unmarshal(fileByte, &c); err != nil {
		return err
	}
	return nil
}

func newConfig() CollectorConfig {
	return CollectorConfig{
		HttpProfiler:  true,
		KafkaProducer: true,
		PollingPeriod: 15,
		IpCommand:     "/opt/mesosphere/bin/detect_ip",
		ConfigPath:    "dcos-metrics-config.yaml",
	}
}

func getNewConfig(args []string) (CollectorConfig, error) {
	c := newConfig()
	thisFlagSet := flag.NewFlagSet("", flag.PanicOnError)
	c.setFlags(thisFlagSet)
	// Override default config with CLI flags if any
	if err := thisFlagSet.Parse(args); err != nil {
		fmt.Println("Errors encountered parsing flags.")
		return c, err
	}

	if err := c.loadConfig(); err != nil {
		return c, err
	}
	fmt.Printf("%+v", c)

	return c, nil
}
