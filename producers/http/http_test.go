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

package http

import (
	"testing"
	"time"

	"github.com/dcos/dcos-go/store"
	"github.com/dcos/dcos-metrics/producers"
	. "github.com/smartystreets/goconvey/convey"
)

func TestJanitor(t *testing.T) {
	Convey("When running the janitor to clean up stale store objects", t, func() {
		// In this test, we set the default CacheExpiry to 120 seconds, which is
		// far longer than this test should ever take to run. testTime then uses
		// the current time (since janitor uses time.Since() to calculate age),
		// so we need to substract seconds from objects in the store
		// (via producers.MetricsMessage.Timestamp) to simulate "stale" objects.
		p := producerImpl{
			config: Config{
				CacheExpiry: 60 * time.Second,
			},
			store:              store.New(),
			janitorRunInterval: 1 * time.Second,
		}
		testTime := time.Now().UTC().Unix()
		testCases := map[string]interface{}{
			"obj0": producers.MetricsMessage{Timestamp: testTime + 30}, // superfresh, expires in 90secs
			"obj1": producers.MetricsMessage{Timestamp: testTime},      // fresh, expires in 60secs
			"obj2": producers.MetricsMessage{Timestamp: testTime - 57}, // fresh, expires in 3secs
			"obj3": producers.MetricsMessage{Timestamp: testTime - 90}, // stale, expired 30secs ago
			"obj4": producers.MetricsMessage{Timestamp: testTime - 90}, // stale, expired 30secs ago
		}
		p.store.Supplant(testCases)

		go p.janitor()
		time.Sleep(2 * p.janitorRunInterval)

		Convey("Should remove only stale objects", func() {
			So(p.store.Size(), ShouldEqual, 3)
			So(p.store.Objects(), ShouldNotContainKey, "obj3")
			So(p.store.Objects(), ShouldNotContainKey, "obj4")
			So(p.store.Objects(), ShouldContainKey, "obj0")
			So(p.store.Objects(), ShouldContainKey, "obj1")
			So(p.store.Objects(), ShouldContainKey, "obj2")
		})

		Convey("Should run on set schedule", func() {
			// Considering that obj2 should expire in 3 seconds, there's a
			// problem if it hasn't been removed in 5 seconds.
			for i := 0; i < 5; i++ {
				if p.store.Size() == 2 {
					break
				}
				time.Sleep(1 * time.Second)
			}
			So(p.store.Size(), ShouldEqual, 2)
			So(p.store.Objects(), ShouldNotContainKey, "obj2")
		})
	})
}
