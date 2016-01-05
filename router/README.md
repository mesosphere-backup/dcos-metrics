# Router Service
Monitoring component to be run as a standalone Mesos Framework.

This service is downstream of the Mesos agents which emit statsd data. It accepts data from those agents over UDP and performs any processing/filtering on those records before forwarding them to one or more customer-managed endpoints. It also runs the configuration service used by the DCOS UI to configure monitoring.