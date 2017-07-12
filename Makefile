.PHONY: all build test clean plugins

all: clean build test

build:
	bash -c "./scripts/build.sh collector"
	bash -c "./scripts/build.sh statsd-emitter"

plugins: clean
	bash -c "./scripts/build.sh plugins"

test:
	bash -c "./scripts/test.sh collector unit"

module:
	docker run \
		--rm \
		-it \
		-v $(PWD):/workspace/dcos-metrics \
		-v $(PWD)/build_module.sh:/workspace/build_module.sh \
		-w /workspace \
		kaskada/cmake-gcc5 \
		bash build_module.sh

clean:
	rm -rf ./build
	rm -rf ./schema/metrics_schema
