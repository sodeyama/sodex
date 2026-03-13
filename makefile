# Root makefile - delegates to src/makefile
# All build artifacts go to build/

.PHONY: all clean remake tools ptest test-qemu test

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
