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

# Validate all files in repo have license header and copyright statement
license_check=0
for source_file in $(find . -type f -name '*.go' -not -path './examples/**' -not -path './vendor/**'); do
    if ! grep -E 'Copyright [0-9]{4} Mesosphere, Inc.' $source_file &> /dev/null; then
        echo "Missing copyright statement in ${source_file}"
        license_check=$((license_check + 1))
    fi
    if ! grep 'Licensed under the Apache License, Version 2.0 (the "License");' $source_file &> /dev/null; then
        echo "Missing license header in ${source_file}"
        license_check=$((license_check + 1))
    fi
done
if [[ $license_check -gt 0 ]]; then
    echo
    echo "ERROR: found ${license_check} cases of missing copyright statements or license headers."
    exit $license_check
fi

# Build components
if [[ $COMPONENT == "collector" ]]; then
    # build metrics_schema package
    pushd "${SOURCE_DIR}/schema"
    go run generator.go -infile metrics.avsc -outfile ./metrics_schema/schema.go
    popd

    # build binary
    go build -a -o ${BUILD_DIR}/dcos-metrics-${COMPONENT}-${GIT_REF}       \
        -ldflags "-X main.VERSION=${VERSION} -X main.REVISION=${REVISION}" \
        *.go

elif [[ $COMPONENT == "statsd-emitter" ]]; then
    go build -a -o ${BUILD_DIR}/dcos-metrics-${COMPONENT}-${GIT_REF} \
    examples/statsd-emitter/main.go
else
    echo "Error: don't know how to build component '${COMPONENT}'!'"
    exit 1
fi
