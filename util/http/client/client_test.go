// +build unit

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
	"net/http"
	"net/http/httptest"
	"net/url"
	"testing"

	httpHelpers "github.com/dcos/dcos-metrics/util/http/helpers"
	. "github.com/smartystreets/goconvey/convey"
)

func TestClient(t *testing.T) {
	Convey("User-Agent should match Git version", t, func() {
		var gotUserAgent string

		ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			td, err := json.Marshal(map[string]string{
				"foo": "bar",
			})
			if err != nil {
				panic(err)
			}

			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusOK)
			w.Write(td)

			gotUserAgent = r.UserAgent()
		}))
		defer ts.Close()

		u, err := url.Parse(ts.URL)
		if err != nil {
			panic(err)
		}

		if err := Fetch(http.DefaultClient, *u, nil); err != nil {
			panic(err)
		}

		// Default is "unset"; this is set by 'scripts/build.sh' via ldflags.
		// The actual test for the version in the built binary is in TestMain,
		// located in dcos-metrics_test.go.
		So(gotUserAgent, ShouldEqual, "unset")
	})
}

func TestFetch(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		td, err := json.Marshal(map[string]string{
			"foo": "bar",
		})
		if err != nil {
			panic(err)
		}

		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		w.Write(td)
	}))
	defer ts.Close()

	testClient, err := httpHelpers.NewMetricsClient("", "")
	if err != nil {
		t.Error("Error retreiving HTTP Client:", err)
	}

	Convey("Should fetch data UNAUTHENTICATED from the Mesos API and return", t, func() {
		var data map[string]string
		host, err := extractHostFromURL(ts.URL)
		if err != nil {
			panic(err)
		}

		testURL := url.URL{
			Scheme: "http",
			Host:   host,
			Path:   "/",
		}
		err = Fetch(testClient, testURL, &data)

		So(data["foo"], ShouldEqual, "bar")
		So(err, ShouldBeNil)
	})

	// TODO(roger): write a test for the auth portion once it's implemented
	Convey("Should fetch data AUTHENTICATED from the Mesos API and return", t, nil)
}

func extractHostFromURL(u string) (string, error) {
	parsed, err := url.Parse(u)
	if err != nil {
		return "", err
	}
	return parsed.Host, nil
}
