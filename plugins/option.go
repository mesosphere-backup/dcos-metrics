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
	"errors"
	"strconv"

	"github.com/dcos/dcos-metrics/producers"
	"github.com/urfave/cli"
)

var (
	errBadProto = errors.New("Protocol can be either http or https only")
	errBadPort  = errors.New("Port must be less than 65535")
	errBadToken = errors.New("Port token can not have zero length")
)

type Option func(*Plugin) error

func ExtraFlags(extraFlags []cli.Flag) Option {
	return func(p *Plugin) error {
		for _, f := range extraFlags {
			p.App.Flags = append(p.App.Flags, f)
		}
		return nil
	}
}

func PollingInterval(i int) Option {
	return func(p *Plugin) error {
		p.PollingInterval = i
		return nil
	}
}

func MetricsProtocol(proto string) Option {
	return func(p *Plugin) error {
		if proto == "http" || proto == "https" {
			p.MetricsProto = proto
			return nil
		}
		return errBadProto
	}
}

func MetricsHost(h string) Option {
	return func(p *Plugin) error {
		p.MetricsHost = h
		return nil
	}
}

func MetricsPort(port int) Option {
	return func(p *Plugin) error {
		if port <= 65535 {
			p.MetricsPort = strconv.Itoa(port)
			return nil
		}
		return errBadPort
	}
}

func MetricsAuthToken(t string) Option {
	return func(p *Plugin) error {
		if len(t) != 0 {
			p.AuthToken = t
			return nil
		}
		return errBadToken
	}
}

func ConnectorFunc(connect func([]producers.MetricsMessage, *cli.Context) error) Option {
	return func(p *Plugin) error {
		p.ConnectorFunc = connect
		return nil
	}
}

func PluginName(n string) Option {
	return func(p *Plugin) error {
		p.Name = n
		return nil
	}
}
