# Root makefile - delegates to src/makefile
# All build artifacts go to build/

.PHONY: all clean remake tools ptest test-qemu test-qemu-curl-https test-qemu-websearch test-qemu-server test-qemu-debug-shell test-qemu-ssh test-agent-bringup test docker-server-image test-docker-server

SODEX_ADMIN_STATUS_TOKEN ?= status-secret
SODEX_ADMIN_CONTROL_TOKEN ?= control-secret
SODEX_ADMIN_ALLOW_IP ?= 10.0.2.2
SODEX_ADMIN_CONFIG_EXTRA ?=
SODEX_EXPECT_CONFIG_ERRORS ?= 0
SODEX_HOST_HTTP_PORT ?= 18080
SODEX_HOST_ADMIN_PORT ?= 10023
SODEX_DOCKER_TIMEOUT ?= 300
SODEX_QEMU_MEM_MB ?= 512
SODEX_QEMU_ACCEL ?=

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

test-qemu-curl-https:
	$(MAKE) -C src test-qemu-curl-https

test-qemu-websearch:
	$(MAKE) -C src test-qemu-websearch

test-qemu-server:
	$(MAKE) -C src test-qemu-server

test-qemu-debug-shell:
	$(MAKE) -C src test-qemu-debug-shell

test-qemu-ssh:
	$(MAKE) -C src test-qemu-ssh

test-agent-bringup:
	$(MAKE) -C src test-agent-bringup

# Host-side unit tests
test:
	$(MAKE) -C tests test

docker-server-image:
	docker build -f docker/server-runtime/Dockerfile -t sodex-server-runtime .

test-docker-server: docker-server-image
	mkdir -p build/log/docker-server-smoke
	SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) \
	SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) \
	SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) \
	SODEX_ADMIN_CONFIG_EXTRA=$(SODEX_ADMIN_CONFIG_EXTRA) \
	SODEX_EXPECT_CONFIG_ERRORS=$(SODEX_EXPECT_CONFIG_ERRORS) \
	SODEX_HOST_HTTP_PORT=$(SODEX_HOST_HTTP_PORT) \
	SODEX_HOST_ADMIN_PORT=$(SODEX_HOST_ADMIN_PORT) \
	SODEX_DOCKER_TIMEOUT=$(SODEX_DOCKER_TIMEOUT) \
	SODEX_QEMU_ACCEL=$(SODEX_QEMU_ACCEL) \
	SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) \
	python3 src/test/run_docker_server_smoke.py sodex-server-runtime build/log/docker-server-smoke
