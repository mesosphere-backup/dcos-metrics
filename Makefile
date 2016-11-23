.PHONY: all build test clean

all: clean build test

build:
	bash -c "./scripts/build.sh collector"
	bash -c "./scripts/build.sh statsd-emitter"

test:
	bash -c "./scripts/test.sh collector unit"

clean:
	rm -rf ./build
	rm -rf ./schema/metrics_schema
