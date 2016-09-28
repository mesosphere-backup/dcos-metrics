package main

import (
	"flag"
	"io/ioutil"
	"os"
	"reflect"
	"testing"
)

func TestDefaultConfig(t *testing.T) {
	testConfig := newConfig()

	testFileType := reflect.TypeOf(testConfig)
	if testFileType.Name() != "CollectorConfig" {
		t.Error("defaultConfig() should return ConfigFile type, got", testFileType.Name())
	}

	if testConfig.PollingPeriod != 15 {
		t.Error("Expected polling period to be 15 by default")
	}

	if !testConfig.HttpProfiler {
		t.Error("Expected HTTP profiler to be enabled by default")
	}

	if !testConfig.KafkaProducer {
		t.Error("Expected Kafka flag to be enabled by default")
	}
}

func TestSetFlags(t *testing.T) {
	testConfig := CollectorConfig{
		ConfigPath: "/some/default/path",
	}
	testFS := flag.NewFlagSet("", flag.PanicOnError)
	testConfig.setFlags(testFS)
	testFS.Parse([]string{"-config", "/another/config/path"})

	if testConfig.ConfigPath != "/another/config/path" {
		t.Error("Expected /another/config/path for config path, got", testConfig.ConfigPath)
	}
}

func TestLoadConfig(t *testing.T) {
	configContents := []byte(`
---
agent_config:
  port: 5051
  metric_topic: agent-metrics
polling_period: 5 
http_profiler: false
kafka_producer: false`)

	tmpConfig, err := ioutil.TempFile("", "testConfig")
	if err != nil {
		panic(err)
	}

	defer os.Remove(tmpConfig.Name())

	if _, err := tmpConfig.Write(configContents); err != nil {
		panic(err)
	}

	testConfig := CollectorConfig{
		ConfigPath: tmpConfig.Name(),
	}

	loadErr := testConfig.loadConfig()

	if loadErr != nil {
		t.Error("Expected no errors loading config, got", loadErr.Error())
	}

	if testConfig.AgentConfig.Port != 5051 {
		t.Error("Expected 5051, got", testConfig.AgentConfig.Port)
	}

	if testConfig.AgentConfig.Topic != "agent-metrics" {
		t.Error("Expected agent-metrics, got", testConfig.AgentConfig.Topic)
	}

	if testConfig.PollingPeriod != 5 {
		t.Error("Expected 15, got", testConfig.PollingPeriod)
	}

	if testConfig.HttpProfiler {
		t.Error("Expected all false config, got", testConfig.HttpProfiler)
	}

	if testConfig.KafkaProducer {
		t.Error("Expected all false config, got", testConfig.KafkaProducer)
	}
}
