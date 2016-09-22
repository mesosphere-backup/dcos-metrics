#VERSION := $(shell git describe --tags)
#LDFLAGS := -X github.com/mesosphere/dcos-metrics/collector/main.VERSION=$(VERSION) -X github.com/mesosphere/dcos-metrics/collector/main.REVISION=$(REVISION)

REVISION := $(shell git rev-parse --short HEAD)

BINARY_NAME := dcos-metrics-collector

FILES := $(shell go list ./... | grep -v vendor)

all: test install

test:
	@echo "+$@"
	go test $(FILES) -v -cover

build: 
	@echo "+$@"
	go build -v -o '$(BINARY_NAME)_$(REVISION)' -ldflags '$(LDFLAGS)' *.go

linux: 
	@echo "+$@"
	GOOS=linux go build -v -o '$(BINARY_NAME)_$(REVISION)'_linux -ldflags '$(LDFLAGS)' *.go 

install:
	@echo "+$@"
	go install -v -o '$(BINARY_NAME)_$(REVISION)' -ldflags '$(LDFLAGS)' $(FILES)
