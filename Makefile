all: clean build-collector test-collector

build-collector:
	bash -c "./scripts/build.sh collector"

test-collector:
	bash -c "./scripts/test.sh collector unit"

clean:
	rm -rf ./build