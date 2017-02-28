.PHONY: all build test clean plugins

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
