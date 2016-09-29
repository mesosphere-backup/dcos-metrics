package collector

import (
	"fmt"
	"io/ioutil"
	"net/http"
)

const (
	userAgent = "metrics-collector/1.0"
)

type HttpCodeError struct {
	Code int
	URI  string
}

func (e HttpCodeError) Error() string {
	return fmt.Sprintf(
		"Got response code when querying %s: %d", e.URI, e.Code)
}
func newHttpCodeError(code int, uri string) HttpCodeError {
	return HttpCodeError{Code: code, URI: uri}
}

func AuthedHttpGet(endpoint string, authToken string) ([]byte, error) {
	request, err := createAuthedHttpGetRequest(endpoint, authToken)
	if err != nil {
		return nil, err
	}
	return httpGet(request)
}

func HttpGet(endpoint string) ([]byte, error) {
	request, err := createHttpGetRequest(endpoint)
	if err != nil {
		return nil, err
	}
	return httpGet(request)
}

func httpGet(request *http.Request) ([]byte, error) {
	client := http.Client{}
	response, err := client.Do(request)
	if err != nil {
		return nil, err
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return nil, newHttpCodeError(response.StatusCode, request.RequestURI)
	}
	return ioutil.ReadAll(response.Body)
}

func createAuthedHttpGetRequest(endpoint string, authToken string) (*http.Request, error) {
	request, err := createHttpGetRequest(endpoint)
	if err != nil {
		return nil, err
	}
	// Configure auth header
	request.Header.Set("Authorization", fmt.Sprintf("token=%s", authToken))
	return request, nil
}

func createHttpGetRequest(endpoint string) (*http.Request, error) {
	request, err := http.NewRequest("GET", endpoint, nil)
	if err != nil {
		return nil, err
	}
	// Configure custom UA header for convenient tracing in mesos logs (or elsewhere)
	request.Header.Set("User-Agent", userAgent)
	return request, nil
}
