# Slave Modules
Monitoring component to be run against mesos-slaves. Contains an Isolator Module (tracks task bringup/shutdown) and a Hook which implements slaveExecutorEnvironmentDecorator (injects monitoring endpoints into Task environments), which must be enabled in the mesos slave via cmdline arguments.

## Prerequisites:

- CMake
- [Mesos' build prerequisites](http://mesos.apache.org/gettingstarted/)
- Boost ASIO (install ```libasio-dev```)

## Build instructions:

Building this module requires a local copy of the mesos source, as well as a build from that source. You can use the provided ```get-mesos.sh``` script to get those.

```
host:dcos-stats$ ... install mesos prereqs ...
host:dcos-stats$ sh get-mesos.sh 0.26.0 # or whatever version you need
```

Once mesos is built, you can build the module code.

```
host:dcos-stats/slave$ sudo apt-get install \
build-essential cmake libasio-dev libboost-system-dev libgoogle-glog-dev
host:dcos-stats/slave$ mkdir -p build; cd build
host:dcos-stats/slave/build$ cmake -Dmesos_VERSION=0.26.0 .. # match version passed to get-mesos.sh
host:dcos-stats/slave/build$ make -j4
host:dcos-stats/slave/build$ make test
```

If you already have a build of mesos available elsewhere, you can just point the stats module to that. For example, here's how to build on a DCOS node, which already has most of what we need within ```/opt/mesosphere```, except for ```libboost_system``` which isn't yet included as of this writing:

```
host:dcos-stats/slave$ sudo yum install cmake boost-system
host:dcos-stats/slave$ mkdir -p build; cd build
host:dcos-stats/slave/build$ cmake \
-Dmesos_INCLUDE_DIR=/opt/mesosphere/include \
-Dmesos_LIBRARY=/opt/mesosphere/lib/libmesos.so \
-Dboost_system_LIBRARY=/usr/lib64/libboost_system.so.1.53.0 \
-DUSE_LOCAL_PICOJSON=false \
-DUSE_LOCAL_PROTOBUF=false \
-DTESTS_ENABLED=false \
..
host:dcos-stats/slave/build$ make -j4
```

## Install instructions

On a system running mesos-slave:

1. Copy ```dcos-stats/slave/build/modules.json``` and ```dcos-stats/slave/build/libstats-slave.so``` to the slave machine.
   * The ```libstats-slave.so``` build must match your version of Mesos. Run ```ldd libstats-slave.so``` to see which version is expected.
   * On DCOS, you will also need to install ```libboost_system.so``` into ```/opt/mesosphere/lib/```. Run ```ldd libstats-slave.so``` to see which version is needed.
2. Customize ```modules.json``` as needed:
   - All parameters must be placed within the ```StatsEnvHook``` section of ```modules.json```, which is where the ```dest_host``` example is provided. If parameters are placed in the wrong section, ```mesos-slave``` will fail to start with an error which says "```These parameters are being dropped!```".
   - The example config has a ```file``` parameter which assumes that ```libstats-slave.so``` is located in ```/home/vagrant/```. Update this parameter to point to where ```libstats-slave.so``` was copied earlier.
   - The example config defaults to outputting stats to ```192.168.33.1:8125```. Update the ```dest_host``` parameter to send stats elsewhere if needed. If the stats destination is running in Marathon, you should be able to use something like ```appname.marathon.mesos``` here.
   - A full list of parameters is documented below under "Customization". Again, place all parameter changes within ```StatsEnvHook``` or else ```mesos-slave``` will fail to start.
3. Enable the module in ```mesos-slave``` by configuring commandline arguments. This step varies depending on your environment:
   * Mesos: Create the following files, or update them if they're already present:
     - Create/modify ```/etc/mesos-slave/modules``` with ```/path/to/your/modules.json```
     - Create/modify ```/etc/mesos-slave/hooks``` with ```com_mesosphere_StatsEnvHook```
     - Modify ```/etc/mesos-slave/isolation``` to contain something like ```posix/cpu,posix/mem,filesystem/posix,com_mesosphere_StatsIsolatorModule```. The content may vary, but ```com_mesosphere_StatsIsolatorModule``` must be present.
   * DCOS: Edit ```/opt/mesosphere/etc/mesos-slave-common``` to contain the following declarations:
     - Add: ```MESOS_MODULES=/path/to/your/modules.json```
     - Add: ```MESOS_HOOKS=com_mesosphere_StatsEnvHook```
     - Update ```MESOS_ISOLATION=cgroups/cpu,cgroups/mem,com_mesosphere_StatsIsolatorModule```. The content may vary, but ```com_mesosphere_StatsIsolatorModule``` must be present.
4. Restart ```mesos-slave```.
   * DCOS:
      1. Delete (or move) the current ```mesos-slave``` state: ```mv /var/lib/mesos/slave /var/lib/mesos/slave.old```
      2. Run ```systemctl restart dcos-mesos-slave```, and ```systemctl status dcos-mesos-slave```.
5. Verify that the module was successfully installed:
   - Use "```ps aux | grep mesos-slave```" to confirm that ```mesos-slave```'s arguments now contain the following values, as configured in step 3:
      - ```--hooks=com_mesosphere_StatsEnvHook```
      - ```--isolation=posix/cpu,posix/mem,filesystem/posix,com_mesosphere_StatsIsolatorModule``` (or as you configured)
      - ```--modules=/home/vagrant/modules.json```
   - Confirm in the logs that the module code is being initialized: ```grep InputAssigner /var/log/mesos/mesos-slave.INFO```. Any parameters you provided in ```modules.json``` should be included here.
      - Something like this: ```Reusing existing stats InputAssigner with parameters: parameter { key: "dest_host" value: "192.168.33.1" }```

## Customization

Available parameters for ```modules.json``` (see also [params.hpp](https://github.com/mesosphere/dcos-stats/blob/master/slave/params.hpp)):

- "```listen_host```" (default ```localhost```): Host to listen on for stats input from tasks.
- "```listen_port_mode```" (default ```ephemeral```): Method to use for opening listen ports for stats input from tasks.
    - "```ephemeral```": Use OS-defined ephemeral ports for listen sockets. See ```/proc/sys/net/ipv4/ip_local_port_range```.
    - "```single```": Use a single port across all tasks on the slave. Only advisable in ip-per-container environments. Requires the following additional arguments:
        - "```listen_port```": Port to listen on in ```single``` mode
    - "```range```": Use a defined range of ports for listening to tasks on the slave. Each task will use one port, monitoring data will be dropped if the number of tasks exceeds the number of ports. Requires the following additional arguments:
        - "```listen_port_start```": Start of range in ```range``` mode (inclusive)
        - "```listen_port_end```": End of range in ```range``` mode (inclusive)
- "```dest_host```" (default ```statsd.monitoring.mesos```): Where to forward stats received from tasks.
- "```dest_refresh_seconds```" (default ```300```, or 5 minutes): Duration in seconds between DNS lookups of ```dest_host```. Automatically detects changes in DNS and redirects output to the new destination.
- "```dest_port```" (default ```8125```): Where to forward stats received from tasks.
- "```annotations```" (default ```true```): Whether to use the [Datadog Tag statsd extension](http://docs.datadoghq.com/guides/dogstatsd/) ([wire format](https://github.com/DataDog/dogstatsd-python/blob/master/statsd.py#L178)) to annotate outgoing stats data with more information about the Mesos task.
- "```chunking```" (default ```true```): Whether to group outgoing data into a smaller number of packets.
- "```chunk_size_bytes```" (default ```512```): Preferred chunk size for outgoing UDP packets, when ```chunking``` is ```true```.
