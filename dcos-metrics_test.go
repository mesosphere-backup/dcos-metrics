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

package main

import (
	"bytes"
	"fmt"
	"os/exec"
	"testing"

	yaml "gopkg.in/yaml.v2"

	. "github.com/smartystreets/goconvey/convey"
)

func TestMain(t *testing.T) {
	// This test assumes that `go build` was run before `go test`,
	// preferably by running `make` at the root of this repo.
	Convey("Version and revision should match Git", t, func() {
		cmd := exec.Command("/bin/bash", "-c", "./build/collector/dcos-metrics-collector* -config /dev/null -version")
		stdout := bytes.Buffer{}
		cmd.Stdout = &stdout
		cmd.Run()
		fmt.Println(stdout.String())
		So(stdout.String(), ShouldContainSubstring, "Version: ")
		So(stdout.String(), ShouldContainSubstring, "Revision: ")
		So(stdout.String(), ShouldContainSubstring, "HTTP User-Agent: ")
		So(stdout.String(), ShouldNotContainSubstring, "unset")
	})
}

func TestProducerIsConfigured(t *testing.T) {
	Convey("When analyzing a ProducersConfig struct to determine if a producer is configured", t, func() {
		Convey("Should return true if a producer configuration was provided", func() {
			var c Config
			mockConfig := []byte(`
---
producers:
    http:
        someConfig: 'someVal'
`)

			if err := yaml.Unmarshal(mockConfig, &c); err != nil {
				panic(err)
			}
			So(producerIsConfigured("http", c), ShouldBeTrue)
		})
		Convey("Should return false if a producer configuration wasn't provided", func() {
			var c Config
			mockConfig := []byte(`
---
producers:
    http:
        someConfig: 'someVal'
`)

			if err := yaml.Unmarshal(mockConfig, &c); err != nil {
				panic(err)
			}
			So(producerIsConfigured("someBogusProducer", c), ShouldBeFalse)
		})
	})
}
