# experiments/glon-pratt/Makefile - GLON-PRATT v004, a Pratt parser written in Glon.
#
# Builds a FRESH copy of the native Glon CLI into ./build from the repository's
# unchanged sources, with the canonical prelude and strings libraries beside it
# (build/glon-lib/), as experiments/glon-latex does. Nothing outside this
# directory is written.
#
#   make             build ./build/glon
#   make test        native tests: s-extra + parser unit tests (pure Glon)
#   make fixtures    write build/fixtures.glon: the v003 fixtures with the
#                    reference (web/infix.js) results, for a native run
#   make test-native-fixtures   run those fixtures through the native parser
#   make test-wasm WASM=path   Node: the same fixtures + random expressions
#                    through glon.wasm, compared with web/infix.js; limits;
#                    Chris figures. WASM must be built from the CURRENT runtime
#                    (CI's demo/shop/glon.wasm): the committed copy is stale.
#   make all-tests   everything above
#   make clean

ROOT    := ../..
CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g
SRCS    := $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c \
           $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
HDRS    := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h
GLON    := build/glon
LIBS    := build/glon-lib/prelude.glon build/glon-lib/strings.glon
NODE    ?= node
WASM    ?=
PARSER  := s-extra.glon pratt.glon math-grammar.glon

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
	$(GLON) s-extra.glon s-extra-tests.glon
	$(GLON) $(PARSER) pratt-tests.glon

fixtures:
	@mkdir -p build
	$(NODE) pratt_node_test.mjs --write-fixtures build/fixtures.glon

test-native-fixtures: all fixtures
	$(GLON) $(PARSER) build/fixtures.glon

test-wasm:
	@test -n "$(WASM)" || { echo "make test-wasm WASM=<glon.wasm built from the CURRENT runtime>"; \
	  echo "(the committed demo/shop/glon.wasm predates the runtime and exhausts its heap far sooner)"; exit 2; }
	$(NODE) pratt_node_test.mjs $(WASM)

all-tests: test test-native-fixtures test-wasm   # needs WASM=

clean:
	rm -rf build

.PHONY: all test fixtures test-native-fixtures test-wasm all-tests clean
