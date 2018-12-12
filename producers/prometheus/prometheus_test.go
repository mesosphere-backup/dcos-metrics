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
	"sort"
	"strconv"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
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

func TestPrometheusProducer(t *testing.T) {
	type testCase struct {
		name   string
		input  []producers.MetricsMessage
		output map[string]*dto.MetricFamily
	}

	now := time.Now().UTC()
	testCases := []testCase{
		// Simple case. One metric with one tag.
		testCase{
			name: "simple case",
			input: []producers.MetricsMessage{
				producers.MetricsMessage{
					Name: "some-message",
					Datapoints: []producers.Datapoint{
						producers.Datapoint{
							Name:  "datapoint-one",
							Value: 123.456,
							Tags:  map[string]string{"hello": "world"},
						},
					},
					Timestamp: now.Unix(),
				},
			},
			output: map[string]*dto.MetricFamily{
				"datapoint_one": &dto.MetricFamily{
					Name: proto.String("datapoint_one"),
					Help: proto.String("DC/OS Metrics Datapoint"),
					Type: dto.MetricType_GAUGE.Enum(),
					Metric: []*dto.Metric{
						&dto.Metric{
							Gauge: &dto.Gauge{Value: proto.Float64(123.456)},
							Label: []*dto.LabelPair{
								&dto.LabelPair{Name: proto.String("cluster_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("container_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("executor_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_name"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_principal"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_role"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("hello"), Value: proto.String("world")},
								&dto.LabelPair{Name: proto.String("hostname"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("mesos_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("task_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("task_name"), Value: proto.String("")},
							},
						},
					},
				},
			},
		},

		// Multiple datapoints with the same name but different sets of tag keys.
		testCase{
			name: "datapoints with shared name and different tags",
			input: []producers.MetricsMessage{
				producers.MetricsMessage{
					Name: "some-message",
					Datapoints: []producers.Datapoint{
						producers.Datapoint{
							Name:  "datapoint-one",
							Value: 123.456,
							Tags:  map[string]string{"tag1": "value1"},
						},
						producers.Datapoint{
							Name:  "datapoint-one",
							Value: 789.012,
							Tags:  map[string]string{"tag2": "value2"},
						},
					},
					Timestamp: now.Unix(),
				},
			},
			// A metric should have labels for all tags provided on all metrics. If there's no corresponding tag for the
			// label on the original metric, its value should be an empty string.
			output: map[string]*dto.MetricFamily{
				"datapoint_one": &dto.MetricFamily{
					Name: proto.String("datapoint_one"),
					Help: proto.String("DC/OS Metrics Datapoint"),
					Type: dto.MetricType_GAUGE.Enum(),
					Metric: []*dto.Metric{
						&dto.Metric{
							Gauge: &dto.Gauge{Value: proto.Float64(123.456)},
							Label: []*dto.LabelPair{
								&dto.LabelPair{Name: proto.String("cluster_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("container_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("executor_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_name"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_principal"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_role"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("hostname"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("mesos_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("tag1"), Value: proto.String("value1")},
								&dto.LabelPair{Name: proto.String("tag2"), Value: proto.String("")}, // empty
								&dto.LabelPair{Name: proto.String("task_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("task_name"), Value: proto.String("")},
							},
						},
						&dto.Metric{
							Gauge: &dto.Gauge{Value: proto.Float64(789.012)},
							Label: []*dto.LabelPair{
								&dto.LabelPair{Name: proto.String("cluster_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("container_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("executor_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_name"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_principal"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("framework_role"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("hostname"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("mesos_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("tag1"), Value: proto.String("")}, // empty
								&dto.LabelPair{Name: proto.String("tag2"), Value: proto.String("value2")},
								&dto.LabelPair{Name: proto.String("task_id"), Value: proto.String("")},
								&dto.LabelPair{Name: proto.String("task_name"), Value: proto.String("")},
							},
						},
					},
				},
			},
		},
	}

	// Ensure the environment is clean
	if err := os.Unsetenv("LISTEN_PID"); err != nil {
		panic(err)
	}
	if err := os.Unsetenv("LISTEN_FDS"); err != nil {
		panic(err)
	}

	for _, testCase := range testCases {
		Convey("Should create a Prometheus server and emit metrics: "+testCase.name, t, func() {
			// Find an available TCP port.
			port, err := getEphemeralPort()
			if err != nil {
				panic(err)
			}

			// Start the server.
			p, metricsChan := New(Config{Port: port})
			go p.Run()
			time.Sleep(1 * time.Second)

			// The web server should be listening on the configured port.
			_, err = net.Dial("tcp", net.JoinHostPort("localhost", strconv.Itoa(port)))
			So(err, ShouldBeNil)

			// Provide metrics to the server from the test case's input.
			for _, metricsMessage := range testCase.input {
				metricsChan <- metricsMessage
			}
			time.Sleep(250 * time.Millisecond)

			// Retrieve the emitted metrics.
			resp, err := http.Get("http://" + net.JoinHostPort("localhost", strconv.Itoa(port)) + "/metrics")
			So(err, ShouldBeNil)
			defer resp.Body.Close()
			// Parse the response body, which should be in the Prometheus exposition format.
			parser := expfmt.TextParser{}
			metricFamilies, err := parser.TextToMetricFamilies(resp.Body)
			So(err, ShouldBeNil)

			// Sort metrics for stable comparisons.
			for _, v := range testCase.output {
				sortMetrics(v.Metric)
			}
			for _, v := range metricFamilies {
				sortMetrics(v.Metric)
			}

			// Compare the retrieved metrics with the test case's expected output.
			So(metricFamilies, ShouldResemble, testCase.output)
		})
	}
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

// sortMetrics sorts m by value in ascending order.
// This function panics if any metric is not a gauge.
func sortMetrics(metrics []*dto.Metric) {
	getValue := func(m dto.Metric) float64 {
		if m.Gauge != nil {
			return m.GetGauge().GetValue()
		}
		panic("Unexpected metric type")
	}
	sort.SliceStable(metrics, func(i, j int) bool { return getValue(*metrics[i]) < getValue(*metrics[j]) })
}
