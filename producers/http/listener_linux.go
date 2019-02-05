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
	"net"
	"os"

	"github.com/coreos/go-systemd/activation"
)

func getListener(unsetEnv bool) ([]net.Listener, error) {
	return activation.Listeners()
}

func getFiles(unsetEnv bool) []*os.File {
	return activation.Files(unsetEnv)
}
