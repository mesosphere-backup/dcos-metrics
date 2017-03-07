# Contributing to Metrics Plugins!
Want to contribute? Great! This document (a constant work in progress) is the place to find out what is important and how to do it.

## Desired Plugins List
Don't see your desired plugin on this list? Make a PR and add it!

- [ ] Elastic Search
- [ ] InfluxDB
- [ ] Prometheus
- [ ] SignalFX

## Where to start?
This project gives a simple interface for building generic plugins for 3rd party metric solutions such as Datadog, SignalFX, etc. The metrics plugin API gives you a easy way to generate a plugin since you only have to worry about the transformation logic, and not the logic around getting metrics from the DC/OS metrics HTTP API.

A `plugin.Plugin{}` type has several functional options. For a list of all options see `plugins/option.go`. 

### Run Book
#### 1. Define flags for your 3rd party provider
These allow you to set configuration that is **in addition** to the flags that the metrics plugin provides. Flags are from `github.com/urfave/cli` type, a basic example from the datadog plugin looks like:
```
myFlags = []cli.Flag{
		cli.StringFlag{
			Name:  "datadog-host",
			Value: datadogHost,
			Usage: "Datadog output hostname",
		},
...
``` 

#### 2. Define a connection function 
This is for transformating metrics from the DC/OS metrics service `[]producers.MetricsMessage{}` type to your 3rd party provider. Your connector func must accept a `[]producers.MetricsMessage{}' type and a urfave/cli `*cli.Context{}` and return an `error`. The connector func is ran in a for loop on the interval specified by `plugin.Plugin.PollingInterval`, an example looks like:
```
datadogConnector = func(metrics []producers.MetricsMessage, c *cli.Context) error {
		if len(metrics) == 0 {
			log.Error("No messages received from metrics service")
		} else {

			for _, m := range metrics {
				log.Info("Sending metrics to datadog...")
				datadogAgent := net.JoinHostPort(c.String("datadog-host"), c.String("datadog-port"))

				if err := toStats(m, datadogAgent); err != nil {
					log.Errorf("Errors encountered sending metrics to Datadog: %s", err.Error())
					continue
				}
			}
		}
		return nil
	}
```

#### 3. Create a `main.go` func
Create a plugin with `plugin.New()` and then start your plugin:
```
	datadogPlugin, err := plugin.New(
		plugin.PluginName("datadog"),
		plugin.ExtraFlags(ddPluginFlags),
		plugin.ConnectorFunc(datadogConnector))
	
	if err != nil {
		log.Fatal(err)
	}

	log.Fatal(datadogPlugin.StartPlugin())

```
