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

type InfluxConfig struct {
	User     string
	Password string
	Port     int
	Host     string
	Database string
}

type KairosConfig struct {
	Blah string
}

type ConsumerConfig struct {
	Kafka          KafkaConfig
	Verbose        bool
	ConsumerSource string
	Destination    string
	Influx         InfluxConfig
	Kairos         KairosConfig
}
