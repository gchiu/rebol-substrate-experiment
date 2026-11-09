# experiments/glon-masm/Makefile -- masm code density and composition.
# Builds from the repository's UNCHANGED sources into ./build.
#
#   make test        variants.glon + tests.glon, native (needs ../glon-mem/build/glon)
#   make wasm-test   the same in glon.wasm (needs ../glon-mem's wasm build)
#   make measure     code cells, tokens and exact S1 instructions per call
#                    (measure-count links the instrumented S1 copy s1_prof.c)
#   make stack       data/return-stack high-water marks of real workloads
ROOT := ../..
EXT  := $(ROOT)/demo/shop/strings-ext.glon
RT   := $(ROOT)/r0_s1_runtime.c
GLON := ../glon-mem/build/glon
NODE ?= $(firstword $(wildcard $(HOME)/emsdk/node/*/bin/node) node)

build/measure-count: measure.c $(RT)
	@mkdir -p build
	cc -std=c17 -O1 -DCOUNT -I$(ROOT) -o $@ measure.c $(RT) $(ROOT)/s1_prof.c
build/measure: measure.c $(RT)
	@mkdir -p build
	cc -std=c17 -O1 -I$(ROOT) -o $@ measure.c $(RT) $(ROOT)/s1.c
build/stackwater: stackwater.c $(RT)
	@mkdir -p build
	cc -std=c17 -O1 -I$(ROOT) -o $@ stackwater.c $(RT) $(ROOT)/s1.c

test:
	$(MAKE) -C ../glon-mem build/glon
	$(GLON) $(EXT) variants.glon tests.glon
wasm-test:
	$(MAKE) -C ../glon-mem build/glon.wasm
	$(NODE) ../glon-mem/run-wasm.js ../glon-mem/build/glon.wasm $(ROOT)/jupyter/prelude.glon \
	  $(ROOT)/demo/shop/strings.glon $(EXT) variants.glon tests.glon
measure: build/measure-count build/measure
	build/measure-count $(ROOT) variants.glon 1000
	build/measure $(ROOT) variants.glon 3000
stack: build/stackwater
	printf 'deep: func [n] [ either = n 0 [ 0 ] [ + 1 deep - n 1 ] ]\nprint deep 80\n' > build/deep.glon
	build/stackwater $(ROOT) $(EXT) ../glon-mem/mem.glon ../glon-mem/mem-tests.glon
	build/stackwater $(ROOT) build/deep.glon
	build/stackwater $(ROOT) $(EXT) ../glon-deflate/inflate.glon ../glon-deflate/tests/unit-tests.glon
	build/stackwater $(ROOT) $(EXT) ../glon-iter/iter.glon ../glon-iter/iter-tests.glon
	build/stackwater $(ROOT) $(EXT) variants.glon tests.glon
	build/stackwater $(ROOT) $(EXT) ../glon-mem/mem.glon variants.glon
clean:
	rm -rf build
.PHONY: test wasm-test measure stack clean
