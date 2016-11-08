# This Vagrant environment provisions a single virtual machine that can be
# used to develop and test the Mesos plugin and metrics collector.
#
# When provisioning is complete, Mesos will be listening on the following
# addresses and ports:
#
#    * Master: http://172.16.99.100:5050
#    * Agent:  http://172.16.99.100:5051
#
# Set values for the three variables below to install specific versions of
# Mesos, Marathon, and Go. Alternately, you may set one or more of these values
# to 'latest' to unpin it.
#
MESOS_RELEASE    = '1.0.1-2.0.93.ubuntu1404'
MARATHON_RELEASE = '1.3.5-1.0.537.ubuntu1404'
GOLANG_RELEASE   = '1.7.3'
IP_ADDRESS       = '172.16.99.100'

Vagrant.configure(2) do |config|
  config.vm.box = 'ubuntu/trusty64'
  config.vm.network 'private_network', ip: IP_ADDRESS

  # Forward ports from the guest to the host for ease of access. All services
  # should still be available at IP_ADDRESS:port as well.
  [5050, 5051, 8080].each do |port|
    config.vm.network 'forwarded_port', guest: port, host: port
  end

  config.vm.provider 'virtualbox' do |vb|
    vb.name   = 'vagrant-mesos'
    vb.cpus   = 2
    vb.memory = 4096
  end

  config.vm.provision 'shell' do |sh|
    sh.path = 'scripts/provision-vagrant.sh'
    args =  [ '--mesos_release',    MESOS_RELEASE  ]
    args += [ '--marathon_release', MARATHON_RELEASE ]
    args += [ '--golang_release',   GOLANG_RELEASE ]
    args += [ '--ip_address',       IP_ADDRESS     ]
    sh.args = args
  end
end
