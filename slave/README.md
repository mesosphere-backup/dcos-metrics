# Slave Modules
Monitoring component to be run against mesos-slaves. Contains an Isolator Module (tracks task bringup/shutdown) and a Hook which implements slaveExecutorEnvironmentDecorator (injects monitoring endpoints into Task environments), which must be enabled in the mesos slave via cmdline arguments.

## Prerequisites:

- CMake
- A nearby Mesos checkout and completed build: Set mesos_SOURCE_DIR and mesos_BUILD_DIR
- Protobuf (preferably what the Mesos build used)
- Boost ASIO (libasio-dev)

## Build instructions:

```
host:dcos-stats/slave$ ... build mesos ...
host:dcos-stats/slave$ sudo apt-get install build-essential cmake libasio-dev libboost-system-dev libgoogle-glog-dev
host:dcos-stats/slave$ mkdir build; cd build
host:dcos-stats/slave/build$ cmake -Dmesos_SOURCE_DIR=/path/to/mesos/ ..
host:dcos-stats/slave/build$ make -j4
```

## Install instructions

On a system running mesos-slave:

1. Copy dcos-stats/slave/build/modules.json and dcos-stats/slave/build/libstats-slave.so to the slave machine.
2. Customize modules.json as needed:
   - Parameters must be placed under the ```StatsEnvHook``` section, where the ```dest_host``` example is provided. Mesos-slave will fail to start with "```These parameters are being dropped!```" if parameters are placed in the wrong section.
   - The example config has a ```file``` parameter which assumes that ```libstats-slave.so``` is located in ```/home/vagrant/```. Update this parameter to point elsewhere if needed.
   - The example config will send stats to ```192.168.33.1:8125```. Update the ```dest_host``` parameter to send somewhere else if needed.
   - Full list of parameters is documented below under "Customization". As mentioned above, keep all parameters against ```StatsEnvHook``` or else mesos-slave will fail on startup.
3. Configure args to enable the module in ```mesos-slave```, creating files as needed:
   - ```/etc/mesos-slave/modules``` should contain "```/path/to/your/modules.json```"
   - ```/etc/mesos-slave/hooks``` should contain "```com_mesosphere_StatsEnvHook```"
   - ```/etc/mesos-slave/isolation``` should contain "```posix/cpu,posix/mem,filesystem/posix,com_mesosphere_StatsIsolatorModule```". This may vary somewhat, but ```com_mesosphere_StatsIsolatorModule``` must be present.
4. Restart ```mesos-slave```.
5. Verify that the module was successfully installed:
   - Use "```ps aux | grep mesos-slave```" to confirm that ```mesos-slave```'s arguments now contain the following values, as configured in step 3:
      - ```--hooks=com_mesosphere_StatsEnvHook```
      - ```--isolation=posix/cpu,posix/mem,filesystem/posix,com_mesosphere_StatsIsolatorModule``` (or as you configured)
      - ```--modules=/home/vagrant/modules.json```
   - Confirm in the logs that the module code is being initialized: ```grep InputAssigner /var/log/mesos/mesos-slave.INFO```. Any parameters you provided in ```modules.json``` should be included here.
      - Something like this: ```Reusing existing stats InputAssigner with parameters: parameter { key: "dest_host" value: "192.168.33.1" }```

## Customization

Available parameters for modules.json (see also params.hpp), which should be placed under :

- "```listen_host```" (default ```localhost```): Host to listen on for stats input from tasks.
- "```listen_port_mode```" (default ```ephemeral```): Method to use for opening listen ports for stats input from tasks.
    - "```ephemeral```": Use OS-defined ephemeral ports for listen sockets. See ```/proc/sys/net/ipv4/ip_local_port_range```.
    - "```single```": Use a single port across all tasks on the slave. Only advisable in ip-per-container environments. Requires the following additional arguments:
        - "```listen_port```": Port to listen on in ```single``` mode
    - "```range```": Use a defined range of ports for listening to tasks on the slave. Each task will use one port, monitoring data will be dropped if the number of tasks exceeds the number of ports. Requires the following additional arguments:
        - "```listen_port_start```": Start of range in ```range``` mode (inclusive)
        - "```listen_port_end```": End of range in ```range``` mode (inclusive)
- "```dest_host```" (default ```statsd.monitoring.mesos```): Where to forward stats received from tasks.
- "```dest_port```" (default ```8125```): Where to forward stats received from tasks.
- "```annotations```" (default ```true```): Whether to use the [Datadog Tag statsd extension](http://docs.datadoghq.com/guides/dogstatsd/) ([wire format](https://github.com/DataDog/dogstatsd-python/blob/master/statsd.py#L178)) to annotate outgoing stats data with more information about the Mesos task.
- "```chunking```" (default ```true```): Whether to group outgoing data into a smaller number of packets.
- "```chunk_size_bytes```" (default ```512```): Preferred chunk size for outgoing UDP packets, when ```chunking``` is ```true```.
