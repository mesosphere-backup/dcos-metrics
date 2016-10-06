package client

import (
	"testing"

	"github.com/dcos/dcos-metrics/consumer/config"
)

var (
	gc = config.ConsumerConfig{
		Influx: config.InfluxConfig{
			User:     "testuser",
			Password: "testpassword",
			Port:     8080,
			Host:     "testhost",
		},
	}

	bc = config.ConsumerConfig{
		Influx: config.InfluxConfig{}}
)

func TestNewInfluxConfig(t *testing.T) {
	if _, err := newInfluxConfig(gc); err != nil {
		t.Error("Expected no errors with clean influx config, got", err.Error())
	}

	if _, err := newInfluxConfig(bc); err == nil {
		t.Error("Expected errors with bad influx config, got", err.Error())
	}

	if c, _ := newInfluxConfig(gc); c.URL.Scheme != "http" && c.URL.Host != "testhost:8080" {
		t.Errorf("Unexpected URL: %+v", c.URL)
	}
}

func TestGetInfluxClient(t *testing.T) {
	_, err := GetInfluxClient(gc)
	if err != nil {
		t.Error(err.Error())
	}
}
