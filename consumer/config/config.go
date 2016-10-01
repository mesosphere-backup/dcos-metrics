package config

var (
	Version  = ""
	Revision = ""
)

type KafkaConfig struct {
	Brokers   string
	Topic     string
	Partition int
	Offset    int64
}

type ConsumerConfig struct {
	Kafka   KafkaConfig
	Verbose bool
	//Influx InfluxConfig
}
