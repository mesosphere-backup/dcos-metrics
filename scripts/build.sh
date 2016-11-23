#!/bin/bash
set -e

COMPONENT="$1"
PLATFORM=$(uname | tr [:upper:] [:lower:])
GIT_REF=$(git describe --always)
SOURCE_DIR=$(git rev-parse --show-toplevel)
BUILD_DIR="${SOURCE_DIR}/build/${COMPONENT}"
VERSION=${GIT_REF}
REVISION=$(git rev-parse --short HEAD)

export PATH="${GOPATH}/bin:${PATH}"
export CGO_ENABLED=0
rm -rf $BUILD_DIR && mkdir -p $BUILD_DIR

if [[ $COMPONENT == "collector" ]]; then
    # build metrics_schema package
    pushd "${SOURCE_DIR}/schema"
    go run generator.go -infile metrics.avsc -outfile ./metrics_schema/schema.go
    popd

    # build binary
    go build -a -o ${BUILD_DIR}/dcos-metrics-${COMPONENT}-${GIT_REF} \
		-ldflags "-X main.VERSION=${VERSION} -X main.REVISION=${REVISION}" \
		*.go

elif [[ $COMPONENT == "statsd-emitter" ]]; then
		go build -a -o ${BUILD_DIR}/dcos-metrics-${COMPONENT}-${GIT_REF} \
		examples/statsd-emitter/main.go
else
    echo "Error: don't know how to build component '${COMPONENT}'!'"
    exit 1
fi
