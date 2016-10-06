package config

import (
	"fmt"
	"os"

	log "github.com/Sirupsen/logrus"

	"github.com/dcos/dcos-metrics/consumer/actions"
	"github.com/dcos/dcos-metrics/consumer/config"
	"github.com/dcos/dcos-metrics/consumer/metric"

	"github.com/urfave/cli"
)

func kafkaFlags(conf *config.ConsumerConfig) []cli.Flag {
	return []cli.Flag{
		cli.StringFlag{
			Name:        "brokers",
			Usage:       "Brokers <host:9092,host:9092,..>",
			EnvVar:      "CONSUMER_CONF_KAFKA_BROKERS",
			Value:       "localhost:9092",
			Destination: &conf.Kafka.Brokers,
		},
		cli.StringFlag{
			Name:        "topic",
			Usage:       "Kafka topic to get",
			EnvVar:      "CONSUMER_CONF_KAFKA_TOPIC",
			Destination: &conf.Kafka.Topic,
		},
		cli.IntFlag{
			Name:        "partition",
			Usage:       "Kafka partition to read from",
			EnvVar:      "CONSUMER_CONF_KAFKA_PARTITION",
			Destination: &conf.Kafka.Partition,
		},
		cli.Int64Flag{
			Name:        "offset",
			Usage:       "Kafka offset",
			Destination: &conf.Kafka.Offset,
		},
	}
}

func globalFlags(conf *config.ConsumerConfig) []cli.Flag {
	return []cli.Flag{
		cli.BoolFlag{
			Name:        "verbose",
			Usage:       "Verbose/debug logging",
			Destination: &conf.Verbose,
		},
	}
}

func setLogger(conf *config.ConsumerConfig) {
	if conf.Verbose {
		log.SetLevel(log.DebugLevel)
		log.Debug("Logger set to verbose/debug")
	}
}

func Execute() {
	app := cli.NewApp()
	conf := &config.ConsumerConfig{}

	app.Version = fmt.Sprintf("%s @ revision %s", config.Version, config.Revision)
	app.Name = "DC/OS Metrics Consumer: Consumer Metrics From Anywhere!"
	app.Flags = globalFlags(conf)
	app.Commands = []cli.Command{
		{
			Name:    "print",
			Aliases: []string{"k"},
			Usage:   "Print metrics to stdout for a given consumer",
			Subcommands: []cli.Command{
				{
					Name:  "kafka",
					Usage: "Use the kafka consumer",
					Flags: kafkaFlags(conf),
					Action: func(c *cli.Context) error {
						// setLogger() must be ran for every action.
						setLogger(conf)
						// Get a new instance of KafkaMetric{}
						km, err := metric.NewKafkaMetric(conf)
						if err != nil {
							return err
						}
						// Pass KafkaMetric{} which implements a MetricConsumer interface{} to
						// the PrintForever() action.
						actions.PrintForever(km)
						return nil
					},
				},
			},
		},
	}
	app.Run(os.Args)

}
