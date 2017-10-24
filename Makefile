.PHONY: all build test clean plugins release

all: clean build test

build:
	bash -c "./scripts/build.sh collector"
	bash -c "./scripts/build.sh statsd-emitter"

plugins: clean
	bash -c "./scripts/build.sh plugins"

test:
	bash -c "./scripts/test.sh collector unit"

clean:
	rm -rf ./build
	rm -rf ./schema/metrics_schema

release:
	rm -rf ./build
	rm -rf ./schema/metrics_schema
	bash -c "./scripts/build.sh collector"
	bash -c "./scripts/build.sh statsd-emitter"
	cp ./build/collector/dcos-metrics-collector* ./release/dcos-metrics
	cp ./build/statsd-emitter/dcos-metrics-statsd-emitte* ./release/statsd-emitter
