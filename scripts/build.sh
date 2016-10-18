#!/bin/bash
set -e

COMPONENT="$1"
PLATFORM=$(uname | tr [:upper:] [:lower:])
GIT_REF=$(git describe --always)
SOURCE_DIR=$(git rev-parse --show-toplevel)
BUILD_DIR="${SOURCE_DIR}/build/${COMPONENT}"

if [[ $COMPONENT == "collector" ]]; then
    export PATH="${GOPATH}/bin:${PATH}"
    export CGO_ENABLED=0
    rm -rf $BUILD_DIR && mkdir -p $BUILD_DIR

    # build metrics_schema package
    pushd "${SOURCE_DIR}/schema"
    go run generator.go -infile metrics.avsc -outfile ./metrics_schema/schema.go
    popd

    # build binary
    go build -a -o ${BUILD_DIR}/dcos-metrics-${COMPONENT}-${GIT_REF} *.go
else
    echo "Error: don't know how to build component '${COMPONENT}'!'"
    exit 1
fi
