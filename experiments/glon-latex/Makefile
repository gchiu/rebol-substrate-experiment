# experiments/glon-latex/Makefile - GLON-LATEX v001 + v002.
#
# Builds a FRESH copy of the native Glon CLI into ./build from the repository's
# unchanged sources (the same GLON_SRCS as the root Makefile's `glon` target),
# with the canonical core libraries copied beside it in build/glon-lib/, where
# the CLI looks for them. Nothing outside this directory is written, and the
# repository's own ./glon binary is left alone.
#
#   make            build ./build/glon
#   make test       build it and run both test suites (v001, then v002)
#   make examples   print a few rendered expressions
#   make samples    print Chris Ross-Gill's math.test.reb figures, in Glon
#   make web        stage the /latex/ page in build/web/latex/, laid out exactly
#                   as deploy-pages.yml publishes it to docs/latex/
#   make web-test   run every page example, and the infix front end
#                   (web/infix.js), through glon.wasm and the vendored KaTeX
#                   (Node; WASM defaults to the shop build)
#   make clean
#
# The suites run as two glon sessions: the loader heap is permanent, and both
# suites' literals together exhaust it (loader_exhausted).

ROOT    := ../..
CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g
SRCS    := $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c \
           $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
HDRS    := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h
GLON    := build/glon
LIBS    := build/glon-lib/prelude.glon build/glon-lib/strings.glon
NODE    ?= node
WASM    ?= $(ROOT)/demo/shop/glon.wasm
WEB     := build/web/latex

all: $(GLON) $(LIBS)

$(GLON): $(SRCS) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(ROOT) -o $@ $(SRCS)

build/glon-lib/prelude.glon: $(ROOT)/jupyter/prelude.glon
	@mkdir -p build/glon-lib
	cp $< $@

build/glon-lib/strings.glon: $(ROOT)/demo/shop/strings.glon
	@mkdir -p build/glon-lib
	cp $< $@

test: all
	$(GLON) latex.glon test-helpers.glon latex-tests.glon
	$(GLON) latex.glon test-helpers.glon chris-samples.glon latex-v002-tests.glon

examples: all
	$(GLON) latex.glon examples.glon

samples: all
	$(GLON) latex.glon chris-samples.glon

web:
	rm -rf $(WEB)
	mkdir -p $(WEB)
	cp web/latex.html $(WEB)/index.html
	cp web/latex-host.js web/latex.css web/infix.js latex.glon $(WEB)/
	cp -r web/katex $(WEB)/katex
	cp $(WASM) $(WEB)/glon.wasm
	cp $(ROOT)/jupyter/prelude.glon $(ROOT)/demo/shop/strings.glon $(WEB)/

web-test:
	$(NODE) web/latex_node_test.mjs $(WASM)
	$(NODE) web/infix_node_test.mjs $(WASM)

clean:
	rm -rf build

.PHONY: all test examples samples web web-test clean
