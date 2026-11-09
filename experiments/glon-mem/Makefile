# experiments/glon-mem/Makefile -- GLON-MEM v001, a minimal memory vocabulary
# prototype (an optional loadable library; no runtime or ABI change).
#
# Builds a fresh native CLI and a standalone WASM module from the repository's
# UNCHANGED sources into ./build. Nothing outside this directory is written.
#
#   make test        native tests (build/glon)
#   make wasm-test   the same tests in build/glon.wasm under node (needs emsdk's emcc
#                    and node: EMSDK=~/emsdk by default)
#   make measure     storage overhead and per-operation cost (native)
#   make clean

ROOT   := ../..
CC     ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -O1
RTSRC  := $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
HDRS   := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h
EXT    := $(ROOT)/demo/shop/strings-ext.glon
EMSDK  ?= $(HOME)/emsdk
EMCC   ?= $(EMSDK)/upstream/emscripten/emcc
NODE   ?= $(firstword $(wildcard $(EMSDK)/node/*/bin/node) node)

all: build/glon

build/glon: $(ROOT)/glon.c $(RTSRC) $(HDRS)
	@mkdir -p build/glon-lib
	$(CC) $(CFLAGS) -I$(ROOT) -o $@ $(ROOT)/glon.c $(RTSRC)
	cp $(ROOT)/jupyter/prelude.glon $(ROOT)/demo/shop/strings.glon build/glon-lib/

test: build/glon
	build/glon $(EXT) mem.glon mem-tests.glon

build/glon.wasm: $(ROOT)/r0_s1_g1a.c $(ROOT)/standalone/glon.c $(ROOT)/s1.c $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_show.c $(HDRS)
	@mkdir -p build
	cd $(ROOT) && EM_NODE_JS=$(NODE) $(EMCC) -O1 -nostdlib -fno-builtin \
	  -s STANDALONE_WASM=1 -s ALLOW_MEMORY_GROWTH=1 -Wl,--no-entry -Wl,--export-memory -I. \
	  standalone/glon.c s1.c r0_s1_runtime.c r0_s1_show.c r0_s1_g1a.c -o experiments/glon-mem/build/glon.wasm

wasm-test: build/glon.wasm
	$(NODE) run-wasm.js build/glon.wasm \
	  $(ROOT)/jupyter/prelude.glon $(ROOT)/demo/shop/strings.glon $(EXT) mem.glon mem-tests.glon

build/measure: measure.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c $(HDRS)
	@mkdir -p build
	$(CC) -std=c17 -O1 -I$(ROOT) -o $@ measure.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c

measure: build/measure
	build/measure $(ROOT)

clean:
	rm -rf build

.PHONY: all test wasm-test measure clean
