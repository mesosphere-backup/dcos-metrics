package main

import (
	"reflect"
	"testing"
)

func TestDefaultConfig(t *testing.T) {
	testConfig := defaultConfig()

	testFileType := reflect.TypeOf(testConfig)
	if testFileType.Name() != "ConfigFile" {
		t.Error("defaultConfig() should return ConfigFile type, got", testFileType.Name())
	}

	if !testConfig.PollAgentEnabled {
		t.Error("Expected agent polling to be enabled by default")
	}

	if !testConfig.HttpProfilerEnabled {
		t.Error("Expected HTTP profiler to be enabled by default")
	}

	if !testConfig.KafkaFlagEnabled {
		t.Error("Expected Kafka flag to be enabled by default")
	}
}
