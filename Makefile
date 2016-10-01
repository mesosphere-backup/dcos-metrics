VERSION := $(shell git describe --tags)
REVISION := $(shell git rev-parse --short HEAD)
LDFLAGS := -X github.com/dcos/dcos-metrics/consumer/config.Version=$(VERSION) -X github.com/dcos/dcos-metrics/consumer/config.Revision=$(REVISION)

all: clean build test

build:
	bash -c "./scripts/build.sh collector"

build-consumer:
	#bash -c "./scripts/build.sh consumer"
	go build -v -o consumer-'$(REVISION)' -ldflags '$(LDFLAGS)' consumer/consumer.go

test:
	bash -c "./scripts/test.sh collector unit"

clean:
	rm -rf ./build
