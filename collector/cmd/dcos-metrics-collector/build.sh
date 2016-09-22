#!/bin/bash

# exit on failures:
set -e

THIS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $THIS_DIR

rm -f collector collector.tgz
go build *.go
strip collector
upx-ucl -9 collector
tar czvf collector.tgz collector
