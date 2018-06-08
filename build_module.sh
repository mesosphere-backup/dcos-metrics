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
apt-get -y install python

# download boost
wget https://downloads.mesosphere.com/pkgpanda-artifact-cache/boost_1_53_0.tar.gz
tar -zxvf boost_1_53_0.tar.gz

# download avro
wget https://downloads.mesosphere.com/pkgpanda-artifact-cache/avro-cpp-1.8.0.tar.gz
tar -zxvf avro-cpp-1.8.0.tar.gz

# download mesos
wget http://repos.mesosphere.com/ubuntu/pool/main/m/mesos/mesos_1.4.1-2.0.1.ubuntu1604_amd64.deb
dpkg --force-all -i mesos_1.4.1-2.0.1.ubuntu1604_amd64.deb

# Set up the modules build dir
pushd dcos-metrics/mesos_module
rm -rf build
mkdir build
popd

# build boost
pushd boost_1_53_0
./bootstrap.sh
cp -r boost /usr/include/
rm -rf /usr/include/boost/phoenix /usr/include/boost/fusion /usr/include/boost/spirit
./b2 --with-filesystem --with-iostreams --with-program_options --with-system -s NO_BZIP2=1
cp -R stage/lib/* /usr/lib
popd
cp -R boost_1_53_0 dcos-metrics/mesos_module/build/boost_1_53_0

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
cp -R avro-cpp-1.8.0 dcos-metrics/mesos_module/build/avro-cpp-1.8.0

# build metrics module
pushd dcos-metrics/mesos_module
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
