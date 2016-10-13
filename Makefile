all: clean build test

build:
	bash -c "./scripts/build.sh collector"

test:
	bash -c "./scripts/test.sh collector unit"

clean:
	rm -rf ./build
	rm -rf ./collector/metrics-schema
	rm -rf ./collector/metrics_schema
