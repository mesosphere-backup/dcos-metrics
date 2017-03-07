package plugin

import "github.com/urfave/cli"

type Option func(*Plugin) error

func ExtraFlags(extraFlags []cli.Flag) Option {
	return func(p *Plugin) error {
		for _, f := range extraFlags {
			p.App.Flags = append(p.App.Flags, f)
		}
		return nil
	}
}
