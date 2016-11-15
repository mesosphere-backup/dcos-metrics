package http

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/dcos/dcos-go/store"
	"github.com/dcos/dcos-metrics/producers"
)

func setup() producerImpl {
	testProdImpl := producerImpl{
		config: Config{
			IP:   "0.0.0.0",
			Port: 9000},
		store:       store.New(),
		metricsChan: make(chan producers.MetricsMessage, 1),
	}

	testProdImpl.metricsChan <- producers.MetricsMessage{
		Name: "test-message",
		Datapoints: []producers.Datapoint{
			{
				Name:  "test-datapoint",
				Value: "test-value",
				Unit:  "test-unit",
			},
		},
		Dimensions: producers.Dimensions{
			ContainerID: "test-container-id",
		},
	}

	return testProdImpl
}

func TestContainersHandler(t *testing.T) {
	req, err := http.NewRequest("GET", "/system/metrics/api/v0/containers", nil)
	if err != nil {
		t.Fatal(err)
	}

	rr := httptest.NewRecorder()

	testProdImpl := setup()
	handlerFunc := containersHandler(&testProdImpl)

	handler := http.HandlerFunc(handlerFunc)

	handler.ServeHTTP(rr, req)

	if status := rr.Code; status != http.StatusOK {
		t.Errorf("Got %v, expected %v", status, http.StatusOK)
	}

	if rr.Body.String() != body {
		t.Errorf("Got %s", rr.Body.String())
	}
}

func TestContainerAppHandler(t *testing.T) {
	testProdImpl := setup()

	req, err := http.NewRequest("GET", "/system/metrics/api/v0/containers/test-container-id/app", nil)
	if err != nil {
		t.Fatal(err)
	}

	rr := httptest.NewRecorder()

	handlerFunc := containerAppHandler(&testProdImpl)
	handler := http.HandlerFunc(handlerFunc)

	handler.ServeHTTP(rr, req)

	if status := rr.Code; status != http.StatusOK {
		t.Errorf("Got %v, expected %v.", status, http.StatusOK)
	}

}
