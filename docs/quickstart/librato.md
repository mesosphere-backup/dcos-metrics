# DC/OS metrics with Librato

In order to send stats to Librato, you will have to install and configure a plugin on every node in your cluster. The
plugin reads from the dcos-metrics API and transmits data to the Librato server. The procedure is similar for all node
types. 

## Prerequisites:

* A cluster running DC/OS 1.9 or newer
* The [DC/OS CLI][docs-dcos-cli] installed
* A Librato account (an account can be created free of charge at [librato.com][librato])

## Deploying the plugin

SSH into every node and become root:
```
$ dcos node ssh --master-proxy --private-ip=<ip-address>
$ sudo su
```

Download the plugin and make sure it's executable
```
$ curl -OL https://downloads.dcos.io/dcos-metrics/plugins/librato /opt/mesosphere/bin/dcos-metrics-librato
$ chmod +x /opt/mesosphere/bin/dcos-metrics-librato
```

Download the systemd configuration for your plugin
```
$ curl -OL https://downloads.dcos.io/dcos-metrics/plugins/librato.service /etc/systemd/system/dcos-metrics-librato.service
```

Change the --dcos-role flag to ‘agent’, ‘agent_public' or ‘master'.
Change the --librato-email flag to your librato email.
Change the --librato-token flag to your librato API token (ensure that it has record permissions).
```
$ vim /etc/systemd/system/dcos-metrics-librato.service
```

Load the new configuration and start the plugin
```
$ systemctl daemon-reload
librato$ systemctl start dcos-metrics-librato.service
```


[docs-dcos-cli]: https://docs.mesosphere.com/latest/cli/
[librato]: https://librato.com
