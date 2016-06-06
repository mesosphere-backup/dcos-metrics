package collector

import (
	"errors"
	"fmt"
	"io/ioutil"
	"net/http"
)

const (
	userAgent = "metrics-collector/1.0"
)

func HttpGet(endpoint string) (body []byte, err error) {
	// Configure custom UA header for tracing in mesos logs
	request, err := http.NewRequest("GET", endpoint, nil)
	if err != nil {
		return nil, err
	}
	request.Header.Set("User-Agent", userAgent)

	client := http.Client{}
	response, err := client.Do(request)
	if err != nil {
		return nil, err
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return nil, errors.New(fmt.Sprintf(
			"Got response code when querying %s: %d", endpoint, response.StatusCode))
	}
	return ioutil.ReadAll(response.Body)
}
