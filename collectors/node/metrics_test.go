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

package node

import (
	"testing"

	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

func TestGetNodeMetrics(t *testing.T) {
	Convey("When getting node metrics, should return nodeMetrics type", t, func() {
		m, err := getNodeMetrics()
		So(err, ShouldBeNil)

		So(len(m), ShouldBeGreaterThan, 0)

		for _, dp := range m {
			So(dp, ShouldHaveSameTypeAs, producers.Datapoint{})
		}
	})
}
