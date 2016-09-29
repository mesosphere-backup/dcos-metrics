#!/bin/bash
# This script performs tests against the dcos-metrics project, specifically:
#
#   * gofmt         (https://golang.org/cmd/gofmt)
#   * goimports     (https://godoc.org/cmd/goimports)
#   * golint        (https://github.com/golang/lint)
#   * go vet        (https://golang.org/cmd/vet)
#   * test coverage (https://blog.golang.org/cover)
#
# It outputs test and coverage reports in a way that Jenkins can understand,
# with test results in JUnit format and test coverage in Cobertura format.
# The reports are saved to build/component/{test-reports,coverage-reports}/*.xml 
#
set -e
SOURCE_DIR=$(git rev-parse --show-toplevel)
BUILD_DIR="${SOURCE_DIR}/build/${COMPONENT}"


function test_collector {
    local test_suite="$1"
    local test_dirs="collector/"
    local package_dirs="./collector/..."
    local ignore_packages=""

    if [[ $test_suite == "unit" ]]; then
        _gofmt
        _goimports
        _golint "$package_dirs"
        _govet "$package_dirs"
        _go_unit_test_with_coverage $test_suite "$package_dirs" "$ignore_packages"
    else
        echo "Unsupported test suite '${test_suite}'"
        exit 1
    fi
}

function logmsg {
    echo -e "\n\n*** $1 ***\n"
}

function _gofmt {
    logmsg "Running 'gofmt' ..."
    gofmt -l -d $(find . -type f -name '*.go' -not -path "./vendor/**") | tee /dev/stderr
}


function _goimports {
    logmsg "Running 'goimports' ..."
    go get -u golang.org/x/tools/cmd/goimports
    goimports -l -d $(find . -type f -name '*.go' -not -path "./vendor/**") | tee /dev/stderr
}


function _golint {
    local package_dirs="$1"
    logmsg "Running 'go lint' ..."
    go get -u github.com/golang/lint/golint
    golint -set_exit_status $test_dirs
}


function _govet {
    local package_dirs="$1"
    logmsg "Running 'go vet' ..."
    go vet $package_dirs
}


function _go_test_with_coverage {
    local test_suite="$1"
    local package_dirs="$2"
    local ignore_packages="$3"
    local covermode="count"
    logmsg "Running unit tests ..."

    go get -u github.com/jstemmer/go-junit-report
    go get -u github.com/smartystreets/goconvey/convey
    go get -u golang.org/x/tools/cmd/cover

    # We can't' use the test profile flag with multiple packages. Therefore,
    # run 'go test' for each package, and concatenate the results into
    # 'profile.cov'.
    echo "mode: ${covermode}" > ${BUILD_DIR}/coverage-reports/profile.cov

    for import_path in $(go list -f={{.ImportPath}} ${package_dirs} | grep -v vendor); do
        package=$(basename ${import_path})
        [[ "$ignore_packages" =~ $package ]] && continue
        go test                                                                  \
            -v                                                                   \
            -race                                                                \
            --tags="$test_suite"                                                 \
            -covermode=$covermode
            -coverprofile="${BUILD_DIR}/coverage-reports/profile_${package}.cov" \
            $import_path                                                         \
            | go-junit-report > "${BUILD_DIR}/test-reports/${package}-report.xml"
    done

    # Concatenate per-package coverage reports into a single file.
    for f in ${BUILD_DIR}/coverage-reports/profile_*.cov; do
        tail -n +2 ${f} >> ${BUILD_DIR}/coverage-reports/profile.cov
        rm $f
    done

    go tool cover -func profile.cov
    gocov convert ${BUILD_DIR}/coverage-reports/profile.cov \
        | gocov-xml > "${BUILD_DIR}/coverage-reports/coverage.xml"
}


# Main. Example usage: ./test.sh collector unit
function main {
    component="$1"
    test_suite="$2"

    if [[ $test_suite != "unit" ]]; then
        echo "Error: only 'unit' is currently supported."
        exit 1
    fi

    if [[ $component != "collector" ]]; then
        echo "Error: only 'collector' is currently supported."
        exit 1
    fi

    test_${component} $test_suite
}


main "$@"
