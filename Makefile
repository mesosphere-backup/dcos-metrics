IMAGE_NAME=dcos-metrics-dev

.PHONY: all
all: clean build test

.PHONY: build
build: docker
	$(call buildIt,collector)
	$(call buildIt,statsd-emitter)

.PHONY: plugins
plugins: docker clean
	$(call buildIt,plugins)

.PHONY: test
test: docker clean build
	$(call testIt,collector unit)

.PHONY: clean
clean:
	rm -rf ./build
	rm -rf ./schema/metrics_schema

.PHONY: docker
docker:
	docker build -t $(IMAGE_NAME) .

define testIt
	$(call containerIt,bash -c "./scripts/test.sh $1")
endef

define buildIt
	$(call containerIt,bash -c "./scripts/build.sh $1")
endef

define containerIt
	docker run \
	--rm \
	-v $(shell pwd):/go/src/github.com/dcos/dcos-metrics \
	-w /go/src/github.com/dcos/dcos-metrics \
	$(IMAGE_NAME) \
	$1
endef

