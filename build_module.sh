#!/bin/bash

set -o errexit -o nounset -o pipefail

# set up compiler flags
export CXXFLAGS="-Wno-deprecated-declarations -Wno-unused"

# install wget
apt-get update
apt-get -y -f install
apt-get -y install wget
apt-get -y install zlib1g-dev
apt-get -y install libcurl4-openssl-dev

# download boost
wget https://downloads.mesosphere.com/pkgpanda-artifact-cache/boost_1_53_0.tar.gz
tar -zxvf boost_1_53_0.tar.gz

# download avro
wget https://downloads.mesosphere.com/pkgpanda-artifact-cache/avro-cpp-1.8.0.tar.gz
tar -zxvf avro-cpp-1.8.0.tar.gz

# download mesos
wget http://repos.mesosphere.com/debian/pool/main/m/mesos/mesos_1.3.0-2.0.3.debian8_amd64.deb
dpkg --force-all -i mesos_1.3.0-2.0.3.debian8_amd64.deb

# build boost
pushd boost_1_53_0
./bootstrap.sh
cp -r boost /usr/include/
rm -rf /usr/include/boost/phoenix /usr/include/boost/fusion /usr/include/boost/spirit
./b2 --with-filesystem --with-iostreams --with-program_options --with-system -s NO_BZIP2=1
mv -v stage/lib/* /usr/lib
popd

# build avro
pushd avro-cpp-1.8.0
rm -rf build
mkdir build
pushd build
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBOOST_ROOT=/usr \
  ..
make -j
make install
popd
popd

# build metrics module
pushd dcos-metrics/mesos_module
rm -rf build
mkdir build
pushd build
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DTESTS_ENABLED=False \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -Dmesos_INCLUDE_DIR=/usr/include \
  -Ddefault_BIN_DIR=/usr/bin \
  -Ddefault_LIB_DIR=/usr/lib \
  -DUSE_LOCAL_PROTOBUF=False \
  -Dprotobuf_INCLUDE_DIR=/usr/lib/mesos/3rdparty/include \
  -DUSE_LOCAL_GLOG=False \
  -Dglog_INCLUDE_DIR=/usr/lib/mesos/3rdparty/include \
  -DUSE_LOCAL_AVRO=False \
  ..
make -j
make install
popd
popd

## Installs lib to $PKG_PATH/
#popd
#
## Copy the json file describing metrics modules.
#module_json="$PKG_PATH/etc/mesos-slave-modules/metrics.json"
#mkdir -p "$(dirname "$module_json")"
#cp /pkg/extra/metrics.json "$module_json"
#
#### Build for Metrics Master & Agent Service ###
#mkdir -p /pkg/src/github.com/dcos
## Create the GOPATH for the go tool to work properly
#mv /pkg/src/dcos-metrics /pkg/src/github.com/dcos/dcos-metrics
#
#pushd /pkg/src/github.com/dcos/dcos-metrics
#make build
#popd
#
#mkdir -p $PKG_PATH/bin
#mv /pkg/src/github.com/dcos/dcos-metrics/build/collector/dcos-metrics-collector-* $PKG_PATH/bin/dcos-metrics
#
## We build this for integration tests
#mv /pkg/src/github.com/dcos/dcos-metrics/build/statsd-emitter/dcos-metrics-statsd-emitter-* $PKG_PATH/bin/statsd-emitter
#
## Create the service file for all roles 
#agent_service="$PKG_PATH/dcos.target.wants_slave/dcos-metrics-agent.service"
#mkdir -p "$(dirname "$agent_service")"
#cp /pkg/extra/dcos-metrics-agent.service "$agent_service"
#
#agent_public_service="$PKG_PATH/dcos.target.wants_slave_public/dcos-metrics-agent.service"
#mkdir -p "$(dirname "$agent_public_service")"
#cp /pkg/extra/dcos-metrics-agent.service "$agent_public_service"
#
#master_service="$PKG_PATH/dcos.target.wants_master/dcos-metrics-master.service"
#mkdir -p "$(dirname "$master_service")"
#cp /pkg/extra/dcos-metrics-master.service "$master_service"
#
## Create socket files for all roles
#agent_socket="$PKG_PATH/dcos.target.wants_slave/dcos-metrics-agent.socket"
#mkdir -p "$(dirname "$agent_socket")"
#cp /pkg/extra/dcos-metrics-agent.socket "$agent_socket"
#
#agent_public_socket="$PKG_PATH/dcos.target.wants_slave_public/dcos-metrics-agent.socket"
#mkdir -p "$(dirname "$agent_public_socket")"
#cp /pkg/extra/dcos-metrics-agent.socket "$agent_public_socket"
#
#master_socket="$PKG_PATH/dcos.target.wants_master/dcos-metrics-master.socket"
#mkdir -p "$(dirname "$master_socket")"
#cp /pkg/extra/dcos-metrics-master.socket "$master_socket"
