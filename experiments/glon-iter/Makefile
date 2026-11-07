# experiments/glon-iter/Makefile - GLON-ITER v001, an iterator vocabulary in Glon.
#
# Builds a FRESH native Glon CLI into ./build from the repository's unchanged
# sources (as experiments/glon-pratt does). Nothing outside this directory is
# written. The frozen Pratt v004 files are only read.
#
#   make              build ./build/glon
#   make test         native unit tests: bytes (iter-tests) and lexer (lexer-tests),
#                     for the adopted iter.glon and for variants/iter-masm.glon
#   make test-lexer-diff   the lexer differential: iter-lexer.glon's tokens over
#                     three fragmentings of 400 inputs (the v003 translation,
#                     error and Chris tables plus seeded random bytes) must equal
#                     the frozen v004 p/token walk, byte for byte (needs Node)
#   make measure      resource measurements (build/measure, build/measure-prof)
#   make all-tests    test + test-lexer-diff
#   make clean

ROOT    := ../..
CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g
RTSRC   := $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
SRCS    := $(ROOT)/glon.c $(RTSRC)
HDRS    := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h
GLON    := build/glon
LIBS    := build/glon-lib/prelude.glon build/glon-lib/strings.glon
NODE    ?= node
EXT     := $(ROOT)/demo/shop/strings-ext.glon
PRATT   := ../glon-pratt

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
	$(GLON) $(EXT) iter.glon iter-tests.glon
	$(GLON) $(EXT) iter.glon iter-lexer.glon lexer-tests.glon
	$(GLON) $(EXT) variants/iter-masm.glon iter-tests.glon
	$(GLON) $(EXT) variants/iter-masm.glon iter-lexer.glon lexer-tests.glon

# Each corpus chunk runs in a fresh process: the loader heap (14,800 cells)
# must hold the libraries plus the chunk's byte literals.
test-lexer-diff: all
	rm -rf build/corpus
	$(NODE) iter_corpus.mjs build/corpus
	@fail=0; n=0; for f in build/corpus/v004-*.glon; do c=$${f#build/corpus/v004-}; c=$${c%.glon}; \
	  $(GLON) $(PRATT)/s-extra.glon $(PRATT)/pratt.glon dump-v004.glon $$f 2>&1 | grep -v '^#\[func\]$$' > build/corpus/v004-$$c.out; \
	  $(GLON) $(EXT) iter.glon iter-lexer.glon dump-iter.glon build/corpus/iter-$$c.glon 2>&1 | grep -v '^#\[func\]$$' > build/corpus/iter-$$c.out; \
	  if cmp -s build/corpus/v004-$$c.out build/corpus/iter-$$c.out; then n=$$((n+1)); else echo "DIFF chunk $$c"; fail=1; fi; \
	done; \
	echo "lexer differential: $$n chunks identical; $$(cat build/corpus/iter-*.out | grep -c '^end\|^error') token walks, $$(cat build/corpus/iter-*.out | grep -vc '^end\|^error') tokens"; \
	test $$fail = 0

build/measure: measure.c $(RTSRC) $(HDRS)
	@mkdir -p build
	$(CC) -std=c17 -O1 -I$(ROOT) -o $@ measure.c $(RTSRC)

build/measure-prof: measure.c $(RTSRC) $(HDRS)
	@mkdir -p build
	$(CC) -std=c17 -O1 -DR0_S1_PROFILE -I$(ROOT) -o $@ measure.c $(RTSRC)

measure: build/measure build/measure-prof
	sh measure.sh 2>&1 | grep -v '^\[dump\]\|^0$$\|^#\[func\]\|^stack sentry'

all-tests: test test-lexer-diff

clean:
	rm -rf build

.PHONY: all test test-lexer-diff measure all-tests clean
