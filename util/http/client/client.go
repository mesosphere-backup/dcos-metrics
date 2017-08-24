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

package client

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"

	log "github.com/Sirupsen/logrus"
)

var (
	// USERAGENT based on $(git describe --always) and set by 'scripts/build.sh'
	USERAGENT = "unset"
	clientLog = log.WithFields(log.Fields{"collector": "http-client"})
)

// Fetch queries an API endpoint and expects to receive JSON. It then unmarshals
// the JSON to the interface{} provided by the caller. By returning data this
// way, Fetch() ensures that JSON is always unmarshaled the same way, and that
// errors are handled correctly, but allows the returned data to be mapped to
// an arbitrary struct that the caller is aware of.
func Fetch(client *http.Client, url url.URL, target interface{}, user string, pass string) error {
	clientLog.Debug("Attempting to request data from ", url.String())
	req, err := http.NewRequest("GET", url.String(), nil)
	if err != nil {
		return err
	}

	req.Header.Set("User-Agent", USERAGENT)
	req.SetBasicAuth(user, pass)

	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	// Special case: on HTTP 401 Unauthorized, exit immediately. Considering this
	// could be running in a goroutine, we don't want the caller to fail forever.
	if resp.StatusCode == http.StatusUnauthorized {
		clientLog.Fatalf("fetch error: unauthorized: please provide a suitable auth token")
	}

	if resp.StatusCode != http.StatusOK {
		e := fmt.Errorf("fetch error: %s", resp.Status)
		clientLog.Error(e)
		return e
	}

	b, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		e := fmt.Errorf("read error: %s: %v", url.String(), err)
		clientLog.Error(e)
		return e
	}

	if err := json.Unmarshal(b, &target); err != nil {
		clientLog.Errorf("Failed to unmarshal JSON from %+v", url)
		return err
	}

	return nil
}
