#!/bin/sh

cd ../../mesos_module/tests

make -j8 standalone_module
if [ $? -ne 0 ]; then
  exit 1
fi

# <input statsd port> <output collector port> <output statsd port>
./standalone_module 8125 8124 ""
