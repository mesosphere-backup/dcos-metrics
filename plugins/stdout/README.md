# Printer plugin for DC/OS Metrics

This plugin simply prints all metrics available on a node to stdout. 

It is intended as an example of how to write a metrics plugin. You should not
run it in production.

## Usage

### Build this plugin (requires a Golang environment)
1. `git clone git@github.com:dcos/dcos-metrics`
1. `cd dcos-metrics && make`

Plugin is available in the build directory:
```
 tree build
build
├── collector
│   └── dcos-metrics-collector-1.0.0-rc7
└── plugins
    └── dcos-metrics-stdout-plugin-1.0.0-rc7
```

Upload the plugin to your node via `scp` or similar, then simply run

`./dcos-metrics-stdout-plugin --dcos-role=agent`

The plugin will log every message it receives to stdout. 

