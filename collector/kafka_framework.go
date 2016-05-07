package collector

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"net"
	"net/http"
)

func connectionEndpoint(framework string) (endpoint string, err error) {
	// SRV lookup to get scheduler's port number:
	// _framework._tcp.marathon.mesos.
	cname, addrs, err := net.LookupSRV(framework, "tcp", "marathon.mesos")
	log.Printf("Lookup SRV %s => %+v\n", cname, addrs)
	if err != nil {
		return "", err
	}
	if len(addrs) == 0 {
		return "", errors.New(fmt.Sprintf("Framework '%s' not found", framework))
	}
	return fmt.Sprintf("http://%s.mesos:%d/v1/connection", framework, addrs[0].Port), nil
}

func httpGet(endpoint string) (body []byte, err error) {
	response, err := http.Get(endpoint)
	if err != nil {
		return nil, err
	}
	return ioutil.ReadAll(response.Body)
}

func extractBrokers(body []byte) (brokers []string, err error) {
	var jsonData map[string]interface{}
	if err = json.Unmarshal(body, &jsonData); err != nil {
		return nil, err
	}
	// expect "dns" entry containing a list of strings
	jsonBrokers := jsonData["dns"].([]interface{})
	brokers = make([]string, len(jsonBrokers))
	for i, jsonDnsEntry := range jsonBrokers {
		brokers[i] = jsonDnsEntry.(string)
	}
	return brokers, nil
}

// Returns a list of broker endpoints, each of the form "host:port"
func LookupBrokers(framework string) (brokers []string, err error) {
	schedulerEndpoint, err := connectionEndpoint(framework)
	if err != nil {
		log.Fatal("Scheduler endpoint lookup failed: ", err)
	}
	body, err := httpGet(schedulerEndpoint)
	if err != nil {
		return nil, err
	}
	return extractBrokers(body)
}
