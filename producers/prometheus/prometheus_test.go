//+build unit

// Copyright 2016 Mesosphere, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package prometheus

import (
	"net"
	"net/http"
	"os"
	"strconv"
	"testing"
	"time"

	dto "github.com/prometheus/client_model/go"
	"github.com/prometheus/common/expfmt"

	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

func TestNew(t *testing.T) {
	Convey("The New method should return a producer and a channel", t, func() {
		pi, pc := New(Config{})
		So(pi, ShouldHaveSameTypeAs, &promProducer{})
		So(pc, ShouldHaveSameTypeAs, make(chan producers.MetricsMessage))
	})
}

func TestRun(t *testing.T) {
	Convey("Should create a Prometheus server on the configured port", t, func() {
		// Ensure the environment is clean
		if err := os.Unsetenv("LISTEN_PID"); err != nil {
			panic(err)
		}
		if err := os.Unsetenv("LISTEN_FDS"); err != nil {
			panic(err)
		}

		port, err := getEphemeralPort()
		if err != nil {
			panic(err)
		}

		p, metricsChan := New(Config{Port: port})
		go p.Run()
		time.Sleep(1 * time.Second)

		// The web server should be listening on the configured TCP port
		_, err = net.Dial("tcp", net.JoinHostPort("localhost", strconv.Itoa(port)))
		So(err, ShouldBeNil)

		// feed in some fake data
		dps := []producers.Datapoint{
			producers.Datapoint{
				Name:  "datapoint-one",
				Value: 123.456,
				Tags:  map[string]string{"hello": "world"},
			},
			producers.Datapoint{
				Name:  "datapoint-one",
				Value: 789.012,
				Tags:  map[string]string{"foo": "bar"},
			},
		}
		metricsChan <- producers.MetricsMessage{
			Name:       "some-message",
			Datapoints: dps,
			Timestamp:  time.Now().UTC().Unix(),
		}
		time.Sleep(250 * time.Millisecond)

		resp, err := http.Get("http://" + net.JoinHostPort("localhost", strconv.Itoa(port)) + "/metrics")
		So(err, ShouldBeNil)
		defer resp.Body.Close()

		parser := expfmt.TextParser{}
		metricFamilies, err := parser.TextToMetricFamilies(resp.Body)
		So(err, ShouldBeNil)

		// Assert the metric is present with the expected type.
		// The metric name should be sanitized for use in the Prometheus exposition format.
		So(metricFamilies, ShouldContainKey, "datapoint_one")
		So(metricFamilies["datapoint_one"].GetType(), ShouldEqual, dto.MetricType_GAUGE)

		// Assert the metric has the expected values and labels.
		// A metric should have labels for all tags provided on all metrics. If there's no corresponding tag for the
		// label on the original metric, its value should be an empty string.
		expectedValueLabels := map[float64]map[string]string{
			123.456: map[string]string{
				"hello": "world",
				"foo":   "",
			},
			789.012: map[string]string{
				"hello": "",
				"foo":   "bar",
			},
		}
		// Collect labels for each metric value into a map for comparison with expectedValueLabels.
		valueLabels := map[float64]map[string]string{}
		for _, metric := range metricFamilies["datapoint_one"].Metric {
			labels := map[string]string{}
			for _, label := range metric.Label {
				if *label.Name == "hello" || *label.Name == "foo" {
					labels[*label.Name] = *label.Value
				}
			}
			valueLabels[metric.GetGauge().GetValue()] = labels
		}
		// Compare the expected and observed value labels.
		So(valueLabels, ShouldResemble, expectedValueLabels)
	})
}

func TestSanitizeName(t *testing.T) {
	Convey("Should remove illegal metric name chars", t, func() {
		io := map[string]string{
			"abc":     "abc",
			"abc123":  "abc123",
			"123abc":  "_123abc",
			"foo-bar": "foo_bar",
			"foo:bar": "foo_bar",
			"foo bar": "foo_bar",
		}
		for i, o := range io {
			So(sanitizeName(i), ShouldEqual, o)
		}
	})
}

func TestAppendIfAbsent(t *testing.T) {
	Convey("Should append to a list only if element is absent", t, func() {
		l := []string{"a", "b", "c"}
		So(appendIfAbsent(l, "a"), ShouldResemble, l)
		So(appendIfAbsent(l, "z"), ShouldResemble, append(l, "z"))
	})
}

// getEphemeralPort returns an available ephemeral port on the system.
func getEphemeralPort() (int, error) {
	addr, err := net.ResolveTCPAddr("tcp", "localhost:0")
	if err != nil {
		return 0, err
	}

	l, err := net.ListenTCP("tcp", addr)
	if err != nil {
		return 0, err
	}
	defer l.Close()

	return l.Addr().(*net.TCPAddr).Port, nil
}
