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

package node

import (
	"testing"

	"github.com/dcos/dcos-metrics/collectors"
)

func TestNew(t *testing.T) {
	cfg := Collector{
		PollPeriod: 1,
	}

	c, chn := New(cfg, collectors.NodeInfo{})
	if c.PollPeriod != 1 {
		t.Error("Expected pollperiod to be 1, got", c.PollPeriod)
	}

	if len(chn) != 0 {
		t.Error("expected empty channel, got", len(chn))
	}
}
