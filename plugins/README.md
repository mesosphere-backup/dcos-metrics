## DC/OS Metrics Service Plugins
**NOTE** These plugins are considered experimental and are not supported by the DC/OS support team. If you're having issues, please feel free to file an issue to this repo (not github.com/dcos/dcos) or file a *Pull Request* (we love pull requests!).

Though your mileage may vary, if there is a plugin missing that you need, please feel free to file an issue so we can poll what services our users want the most. 

## Developing
To develop a new plugin in preparation for a pull requet to this project:

### Plugin Development
1. Create a new package for your `cool` plugin `mkdir plugins/cool/`
1. Make a go file named after your package `touch plugins/cool/cool.go`

#### Get a new plugin.Plugin{}
1. Create flags: `myFalgs := []cli.Flag{}`
1. `myPlugin := plugin.New(myFlags)` -> `plugin.Plugin{}`
1. Use `myPlugin.App.Action` and pass it a [cli.ActionFunc()](https://github.com/urfave/cli/blob/master/app.go#L66) which has all the `main()` logic your plugin needs.
1. Call `myPlugin.Metrics()` which returns a slice of `producers.MetricsMessage{}` from the metrics HTTP API. 

At this point you can write what ever helper methods you need to transform these and send to your metrics aggregation or cloud hosted service. 

### Unit Test Coverage
1. Add a unit test (aim for 80% coverage)

### Add Plugin to Build Pipeline
#### Add `cool` plugin to the build pipeline
```
vi Makefile
# add a line to build your plugin from scripts/build.sh - follow the already shown 
pattern for this.

vi scripts.build.sh
# add in a function() for building your plugin. Follow the patterns in this script for 
adding your function.
```

#### Add a README.md in your `cool` package explaining:
```
vi plugins/cool/README.md
# The Cool Plugin
This cool plugin allows you to plugin to cool stuff.

# How To Run Me
1. ...

# Caveats
I knew there were caveats!
``` 

#### Submit a Pull Request! 
Ping @malnick on DC/OS community slack :)
