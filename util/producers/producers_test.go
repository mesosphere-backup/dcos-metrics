// +build unit

// Copyright 2017 Mesosphere, Inc.
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

package producers

import (
	"testing"

	. "github.com/smartystreets/goconvey/convey"
)

func TestSortTags(t *testing.T) {
	Convey("When sorting tags", t, func() {
		someTags := map[string]string{
			"foo":   "bar",
			"baz":   "qux",
			"corge": "grault",
		}
		sortedTags := SortTags(someTags)

		Convey("all keys should be returned as a pair", func() {
			So(len(sortedTags), ShouldEqual, 3)
		})

		Convey("tags should be sorted by key", func() {
			So(sortedTags[0], ShouldResemble, []string{"baz", "qux"})
			So(sortedTags[1], ShouldResemble, []string{"corge", "grault"})
			So(sortedTags[2], ShouldResemble, []string{"foo", "bar"})
		})
	})
}
