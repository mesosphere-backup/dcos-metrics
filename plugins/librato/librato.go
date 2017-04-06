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

package main

import (
	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/plugins"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/urfave/cli"
)

var (
	libratoEmailFlagName  = "librato-email"
	libratoTokenFlagName  = "librato-token"
	libratoPrefixFlagName = "librato-metric-prefix"
	libratoUrl            = "https://metrics-api.librato.com"
	pluginFlags           = []cli.Flag{
		cli.StringFlag{
			Name:  libratoEmailFlagName,
			Usage: "Librato user email address",
		},
		cli.StringFlag{
			Name:  libratoTokenFlagName,
			Usage: "Librato user API token (must have record access)",
		},
		cli.StringFlag{
			Name:  libratoPrefixFlagName,
			Usage: "Metric name prefix applied to all metrics sent to Librato",
			Value: "dcos",
		},
	}
)

func main() {
	log.Info("Starting Librato DC/OS metrics plugin")
	libratoPlugin, err := plugin.New(
		plugin.Name("librato"),
		plugin.ExtraFlags(pluginFlags),
		plugin.ConnectorFunc(func(metrics []producers.MetricsMessage, context *cli.Context) error {
			log.Infof("Processing %d metrics", len(metrics))
			opts := &postRequestOpts{
				libratoUrl:      libratoUrl,
				libratoEmail:    context.String(libratoEmailFlagName),
				libratoToken:    context.String(libratoTokenFlagName),
				pollingInterval: context.Int64("polling-interval"),
				metricPrefix:    context.String(libratoPrefixFlagName),
			}
			post, err := newPostRequest(opts)
			if err != nil {
				log.Errorf("Could not build post request: %v", err)
				return nil
			}
			post.add(metrics)
			if err := post.send(); err != nil {
				log.Errorf("Could not post to Librato: %v", err)
				return nil
			}
			return nil
		}))
	if err != nil {
		log.Fatal(err)
	}
	log.Fatal(libratoPlugin.StartPlugin())
}
