package collector

import (
	"fmt"
	"io/ioutil"
	"net/http"
)

const (
	userAgent = "metrics-collector/1.0"
)

// HTTPCodeError ...
type HTTPCodeError struct {
	Code int
	URI  string
}

func (e HTTPCodeError) Error() string {
	return fmt.Sprintf(
		"Got response code when querying %s: %d", e.URI, e.Code)
}
func newHTTPCodeError(code int, uri string) HTTPCodeError {
	return HTTPCodeError{Code: code, URI: uri}
}

// AuthedHTTPGet ...
func AuthedHTTPGet(endpoint string, authToken string) ([]byte, error) {
	request, err := createAuthedHTTPGetRequest(endpoint, authToken)
	if err != nil {
		return nil, err
	}
	return httpGet(request)
}

// HTTPGet ...
func HTTPGet(endpoint string) ([]byte, error) {
	request, err := createHTTPGetRequest(endpoint)
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
		return nil, newHTTPCodeError(response.StatusCode, request.RequestURI)
	}
	return ioutil.ReadAll(response.Body)
}

func createAuthedHTTPGetRequest(endpoint string, authToken string) (*http.Request, error) {
	request, err := createHTTPGetRequest(endpoint)
	if err != nil {
		return nil, err
	}
	// Configure auth header
	request.Header.Set("Authorization", fmt.Sprintf("token=%s", authToken))
	return request, nil
}

func createHTTPGetRequest(endpoint string) (*http.Request, error) {
	request, err := http.NewRequest("GET", endpoint, nil)
	if err != nil {
		return nil, err
	}
	// Configure custom UA header for convenient tracing in mesos logs (or elsewhere)
	request.Header.Set("User-Agent", userAgent)
	return request, nil
}
