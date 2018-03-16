# DC/OS metrics with DataDog

In order to send stats to DataDog, you will have to install and configure a plugin on every node in your cluster. The
plugin reads from the dcos-metrics API and transmits data to the DataDog server. The procedure is similar for all node
types. 

## Prerequisites:

* A cluster running DC/OS 1.9 or newer
* The [DC/OS CLI][docs-dcos-cli] installed
* A DataDog account (an account can be created free of charge at [datadoghq.com][datadog-hq]

## Deploying the plugin

SSH into every node and become root:
```
$ dcos node ssh --master-proxy --private-ip=<ip-address>
$ sudo su
```

Download the plugin and make sure it's executable
```
$ curl -OL https://downloads.dcos.io/dcos-metrics/plugins/datadog /opt/mesosphere/bin/dcos-metrics-datadog
$ chmod +x /opt/mesosphere/bin/dcos-metrics-datadog
```

Download the systemd configuration for your plugin
```
$ curl -OL https://downloads.dcos.io/dcos-metrics/plugins/datadog.service /etc/systemd/system/dcos-metrics-datadog.service
```

Change the --dcos-role flag to ‘agent’, ‘agent_public' or ‘master'.
Change the --datadog-key flag to your DataDog API key.
```
$ vim /etc/systemd/system/dcos-metrics-datadog.service
```

Load the new configuration and start the plugin
```
$ systemctl daemon-reload
$ systemctl start dcos-metrics-datadog.service
```


[docs-dcos-cli]: https://docs.mesosphere.com/latest/cli/
[datadog-hq]: https://datadoghq.com
