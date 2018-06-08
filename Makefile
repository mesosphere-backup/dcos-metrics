.PHONY: all build test clean plugins

all: clean build test

build:
	bash -c "./scripts/build.sh collector"
	bash -c "./scripts/build.sh statsd-emitter"

plugins: clean
	bash -c "./scripts/build.sh plugins"

test:
	bash -c "./scripts/test.sh collector unit"

.PHONY: build-docker-image
build-docker-image:
	mkdir -p mesos_module/build/docker
	cp Dockerfile mesos_module/build/docker
	cd mesos_module/build/docker; \
	docker build -t dcos-metrics-image .

.PHONY: module
module: build-docker-image
	docker run \
		--rm \
		-it \
		-v $(PWD):/workspace/dcos-metrics \
		-v $(PWD)/build_module.sh:/workspace/build_module.sh \
		-w /workspace \
		dcos-metrics-image \
		build_module.sh

.PHONY: run-module
run-module:
	bash run_module.sh

.PHONY: clean
clean:
	rm -rf ./build
	rm -rf ./schema/metrics_schema
