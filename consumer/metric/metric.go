package metric

import "time"

type Message struct {
	Key       string
	Value     string
	Topic     string
	Timestamp time.Time
}

type Metric struct {
	IO chan Message
}

type MetricConsumer interface {
	GetMessageChan() chan Message
	SetupMessageChan() error
}
