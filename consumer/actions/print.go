package actions

import (
	"errors"
	"os"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/consumer/metric"
)

func validateMessage(msg metric.Message) error {
	if msg.Key == "" {
		return errors.New("Message key is empty")
	}
	if msg.Value == "" {
		return errors.New("Message value is empty")
	}
	return nil
}

func printMessage(msg metric.Message) {
	if err := validateMessage(msg); err != nil {
		// Log the error but still try to print empty data.
		log.Error(err)
	}
	log.WithFields(log.Fields{
		"Key":       msg.Key,
		"Value":     msg.Value,
		"Timestamp": msg.Timestamp}).Info("RECEIVED")
}

func PrintLastMessage(m metric.Metricer) error {
	m.SetupMessageChan()
	msgs := m.GetMessageChan()

	select {
	case msg := <-msgs:
		printMessage(msg)
	case _ = <-time.After(1 * time.Second):
		return errors.New("No message to print.")
	}
	return nil
}

func PrintForever(m metric.Metricer) error {
	m.SetupMessageChan()
	msgs := m.GetMessageChan()
	for {
		log.Debug("Checking queue...")
		select {
		case msg := <-msgs:
			printMessage(msg)
		case _ = <-time.After(2 * time.Second):
			log.Warn("Message channel is empty, sleepig for 2 seconds.")
		}
	}
	return nil
}

func check(err error) {
	if err != nil {
		log.Error(err)
		os.Exit(1)
	}
}
