# experiments/glon-deflate/Makefile -- a streaming DEFLATE decompressor in Glon.
#
# Builds a FRESH native Glon CLI and the C stand-in host into ./build from the
# repository's UNCHANGED sources. Nothing outside this directory is written.
#
#   make              build/glon (CLI) and build/dzhost (GLON_LIVE byte-event host)
#   make unit         Glon unit tests (CLI): bit reader, tables, small streams
#   make test         the full conformance corpus (fixtures vs Python's zlib)
#   make demo         the ASCII progress bar on a generated file
#   make bench        throughput / memory / suspension benchmarks
#   make repro        the runtime GC defect, in plain Glon (README.md)
#   make clean

ROOT    := ../..
CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O1
RTSRC   := $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
LIVESRC := $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
HDRS    := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h
EXT     := $(ROOT)/demo/shop/strings-ext.glon
PY      ?= python3

all: build/glon build/glon-lib/prelude.glon build/glon-lib/strings.glon build/dzhost

build/glon: $(ROOT)/glon.c $(RTSRC) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(ROOT) -o $@ $(ROOT)/glon.c $(RTSRC)

build/glon-lib/prelude.glon: $(ROOT)/jupyter/prelude.glon
	@mkdir -p build/glon-lib
	cp $< $@

build/glon-lib/strings.glon: $(ROOT)/demo/shop/strings.glon
	@mkdir -p build/glon-lib
	cp $< $@

build/dzhost: dzhost.c $(LIVESRC) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -DGLON_LIVE -I$(ROOT) -o $@ dzhost.c $(LIVESRC)

unit: all
	build/glon $(EXT) f2buf.glon inflate.glon tests/unit-tests.glon

test: all unit
	$(PY) -I tests/run_tests.py

demo: all
	$(PY) -I tests/demo.py

bench: all
	$(PY) -I tests/bench.py

# prints "corrupted at 0 after spraying from 2078" and 11 while the defect is
# present; "intact" once the collector rejects interior pointers
repro: all
	build/glon $(EXT) tests/gc-interior-repro.glon

clean:
	rm -rf build

.PHONY: all unit test demo bench repro clean
