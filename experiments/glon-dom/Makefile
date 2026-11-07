# experiments/glon-dom/Makefile - GLON-DOM v001, a DOM-tree event iterator in Glon.
#
# Builds a FRESH native Glon CLI into ./build from the repository's unchanged
# sources (as experiments/glon-iter does). Nothing outside this directory is
# written. Iterator v001 (../glon-iter) is only read: iter.glon is loaded, and
# its measure.c is compiled here.
#
#   make              build ./build/glon
#   make test         the walker (dom-tests), garbage collection during walks
#                     (gc-tests), a 150-level chain (deep-tests) and the
#                     explicit-state alternative (fn-tests); each in a fresh session
#   make measure      resource measurements (build/measure, build/measure-prof)
#   make clean

ROOT    := ../..
CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g
RTSRC   := $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
SRCS    := $(ROOT)/glon.c $(RTSRC)
HDRS    := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h
GLON    := build/glon
LIBS    := build/glon-lib/prelude.glon build/glon-lib/strings.glon
EXT     := $(ROOT)/demo/shop/strings-ext.glon
ITER    := ../glon-iter/iter.glon
WALK    := $(EXT) $(ITER) dom.glon
MEASURE := ../glon-iter/measure.c

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

# The CLI prints #[func] for each wrapped library; the test files print their
# counts and a verdict, and exit non-zero on any failure.
test: all
	@set -e; for t in dom-tests gc-tests deep-tests; do \
	  $(GLON) $(WALK) t-common.glon $$t.glon > build/$$t.out 2>&1 || { cat build/$$t.out; exit 1; }; \
	  grep -v '^#\[func\]$$' build/$$t.out; done
	@$(GLON) $(WALK) variants/dom-fn.glon t-common.glon fn-tests.glon > build/fn-tests.out 2>&1 || { cat build/fn-tests.out; exit 1; }; \
	  grep -v '^#\[func\]$$' build/fn-tests.out

build/measure: $(MEASURE) $(RTSRC) $(HDRS)
	@mkdir -p build
	$(CC) -std=c17 -O1 -I$(ROOT) -o $@ $(MEASURE) $(RTSRC)

build/measure-prof: $(MEASURE) $(RTSRC) $(HDRS)
	@mkdir -p build
	$(CC) -std=c17 -O1 -DR0_S1_PROFILE -I$(ROOT) -o $@ $(MEASURE) $(RTSRC)

measure: all build/measure build/measure-prof
	sh measure.sh 2>&1 | grep -v '^\[dump\]\|^0$$\|^#\[func\]\|^stack sentry'

clean:
	rm -rf build

.PHONY: all test measure clean
