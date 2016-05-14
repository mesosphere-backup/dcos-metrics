package collector

import (
	"errors"
	"fmt"
	"io/ioutil"
	"net/http"
)

func httpGet(endpoint string) (body []byte, err error) {
	response, err := http.Get(endpoint)
	if err != nil {
		return nil, err
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return nil, errors.New(fmt.Sprintf(
			"Got response code when querying %s: %d", endpoint, response.StatusCode))
	}
	return ioutil.ReadAll(response.Body)
}
