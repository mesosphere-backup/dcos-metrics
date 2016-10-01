package actions

import (
	"testing"

	"github.com/dcos/dcos-metrics/consumer/metric"
)

func TestValidateMessage(t *testing.T) {
	if err := validateMessage(metric.Message{Key: ""}); err == nil {
		t.Error("Expected validateMessage() to throw error, got", err)
	}
	if err := validateMessage(metric.Message{Value: ""}); err == nil {
		t.Error("Expected validateMessage() to throw error, got", err)
	}
}
