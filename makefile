# Root makefile - delegates to src/makefile
# All build artifacts go to build/

.PHONY: all clean remake tools ptest test-qemu test docker-server-image test-docker-server

all:
	$(MAKE) -C src

clean:
	$(MAKE) -C src clean

remake:
	$(MAKE) -C src remake

tools:
	$(MAKE) -C src tools

ptest:
	$(MAKE) -C src ptest

test-qemu:
	$(MAKE) -C src test-qemu

# Host-side unit tests
test:
	$(MAKE) -C tests test

docker-server-image:
	docker build -f docker/server-runtime/Dockerfile -t sodex-server-runtime .

test-docker-server: docker-server-image
	mkdir -p build/log/docker-server-smoke
	python3 src/test/run_docker_server_smoke.py sodex-server-runtime build/log/docker-server-smoke
