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

import "sort"

// SortTags turns a map[string]string into a slice of key/values, sorted by key.
func SortTags(tags map[string]string) [][]string {
	keys := make([]string, len(tags))
	result := make([][]string, len(tags))
	i := 0
	for k := range tags {
		keys[i] = k
		i++
	}
	sort.Strings(keys)
	for i, k := range keys {
		result[i] = []string{k, tags[k]}
	}
	return result
}
