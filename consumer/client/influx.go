package client

import (
	"errors"
	"fmt"
	"net/url"

	"github.com/dcos/dcos-metrics/consumer/config"
	influxClient "github.com/influxdata/influxdb/client"
)

func newInfluxConfig(consumerConf config.ConsumerConfig) (influxClient.Config, error) {
	ic := influxClient.Config{}

	if len(consumerConf.Influx.Host) == 0 {
		return ic, errors.New("Hostname must be defined to use the influx client, try --host")
	}

	if consumerConf.Influx.Port == 0 {
		return ic, errors.New("Port must be defined to use the influx client, try --port")
	}

	url, err := url.Parse(fmt.Sprintf("http://%s:%d", consumerConf.Influx.Host, consumerConf.Influx.Port))
	if err != nil {
		return ic, err
	}

	ic.URL = *url

	if len(consumerConf.Influx.User) > 0 && len(consumerConf.Influx.Password) == 0 {
		return ic, errors.New("Password must be present when --user is passed. Try --password.")
	}

	ic.Username = consumerConf.Influx.User
	ic.Password = consumerConf.Influx.Password

	return ic, nil
}

func GetInfluxClient(consumerConf config.ConsumerConfig) (*influxClient.Client, error) {
	nullClient := &influxClient.Client{}
	influxConfig, err := newInfluxConfig(consumerConf)
	if err != nil {
		return nullClient, errors.New(err.Error())
	}

	ic, err := influxClient.NewClient(influxConfig)
	if err != nil {
		return nullClient, errors.New(err.Error())
	}
	return ic, nil
}
