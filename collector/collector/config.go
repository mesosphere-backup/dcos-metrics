package main

import (
	"flag"
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
	fileByte, err := ioutil.ReadFile(c.ConfigPath)
	if err != nil {
		return err
	}

	if err = yaml.Unmarshal(fileByte, &c); err != nil {
		return err
	}
	return nil
}

func defaultConfig() configFile {
	return ConfigFile{
		PollAgentEnabled:    true,
		HttpProfilerEnabled: true,
		KafkaFlagEnabled:    true,
		ConfigPath:          "dcos-metrics-config.yaml",
	}
}

func parseArgsReturnConfig(args []string) (ConfigFile, error) {
	c := defaultConfig()
	thisFlagSet := flag.NewFlagSet("", flag.ContinueOnError)
	c.setFlags(thisFlagSet)
	// Override default config with CLI flags if any
	if err := thisFlagSet.Parse(args); err != nil {
		return c, err
	}
	if err := c.loadConfig(); err != nil {
		return c, err
	}
	return c, nil
}
