#!/bin/bash
set -e
export PATH="${GOPATH}/bin:${PATH}"
export CGO_ENABLED=0

COMPONENT="$1"
PLATFORM=$(uname | tr [:upper:] [:lower:])
GIT_REF=$(git describe --always)
SOURCE_DIR=$(git rev-parse --show-toplevel)
BUILD_DIR="${SOURCE_DIR}/build/${COMPONENT}"

# Bring in dependencies required by schema/generator.go
go get -u github.com/antonholmquist/jason

rm -rf $BUILD_DIR && mkdir -p $BUILD_DIR
cd "${SOURCE_DIR}/${COMPONENT}"

# build metrics-schema package
go generate

# build binary
cd collector
go build -a -o ${BUILD_DIR}/dcos-metrics-collector-${GIT_REF} *.go
