package main

import (
	"flag"
	"fmt"
	"io/ioutil"

	yaml "gopkg.in/yaml.v2"
)

type ConfigFile struct {
	PollAgentEnabled    bool `json:"poll_agent"`
	HttpProfilerEnabled bool `json:"http_profiler_enabled"`
	KafkaFlagEnabled    bool `json:"kafka_flag_enabled"`
	ConfigPath          string
}

func (c *ConfigFile) setFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.ConfigPath, "config", c.ConfigPath, "The path to the config file.")
}

func (c *ConfigFile) loadConfig() error {
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

func defaultConfig() ConfigFile {
	return ConfigFile{
		PollAgentEnabled:    true,
		HttpProfilerEnabled: true,
		KafkaFlagEnabled:    true,
		ConfigPath:          "dcos-metrics-config.yaml",
	}
}

func parseArgsReturnConfig(args []string) (ConfigFile, error) {
	c := defaultConfig()
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
	return c, nil
}
