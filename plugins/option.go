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

package plugin

import (
	"github.com/dcos/dcos-metrics/producers"
	"github.com/urfave/cli"
)

// Option lets each plugin configure the Plugin type. The plugin.New(...)
// method will call each supplied Option before returning the initialized
// Plugin.
type Option func(*Plugin) error

// ExtraFlags sets additional cli.Flag's on the Plugin
func ExtraFlags(extraFlags []cli.Flag) Option {
	return func(p *Plugin) error {
		for _, f := range extraFlags {
			p.App.Flags = append(p.App.Flags, f)
		}
		return nil
	}
}

// PollingInterval sets the polling interval to the supplied value.
func PollingInterval(i int) Option {
	return func(p *Plugin) error {
		p.PollingInterval = i
		return nil
	}
}

// ConnectorFunc is what the plugin framework will call once it has gathered
// metrics. It is expected that this function will convert these messages to
// a 3rd party format and then send the metrics to that service.
func ConnectorFunc(connect func([]producers.MetricsMessage, *cli.Context) error) Option {
	return func(p *Plugin) error {
		p.ConnectorFunc = connect
		return nil
	}
}

// Name allows the plugin to set a custom name for itself.
func Name(n string) Option {
	return func(p *Plugin) error {
		p.Name = n
		return nil
	}
}
