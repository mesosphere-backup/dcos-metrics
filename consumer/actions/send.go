package actions

import (
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/consumer/metric"
	influxClient "github.com/influxdata/influxdb/client"
)

func SendToInflux(ic *influxClient.Client, m metric.MetricConsumer) error {
	m.SetupMessageChan()
	msgChan := m.GetMessageChan()

	pts := []influxClient.Point{}
	sampleSize := 1000

	for sample := 0; sample < sampleSize; sample++ {
		select {
		case msg := <-msgChan:
			log.WithFields(log.Fields{
				"Key":       msg.Key,
				"Value":     msg.Value,
				"Timestamp": msg.Timestamp}).Debug("RECEIVED")

			if dur, ver, err := ic.Ping(); err != nil {
				log.Warnf("Unable to connect to influxDB: %s - %v, %s", err.Error(), dur, ver)
				break
			}

			pt := influxClient.Point{
				Measurement: msg.Key,
				Fields: map[string]interface{}{
					"value": msg.Value,
				},
				Time:      time.Now(),
				Precision: "s",
			}

			pts = append(pts, pt)

			if sample == sampleSize {
				bps := influxClient.BatchPoints{
					Points:          pts,
					Database:        "foo",
					RetentionPolicy: "default",
				}

				if _, err := ic.Write(bps); err != nil {
					return (err)
				}
			}

		case _ = <-time.After(1 * time.Second):
			log.Warn("No messages received from Kafka after 1 second.")
		}
	}
	return nil
}

func SendToKairos() {}
