# experiments/glon-dom-links/Makefile - extract hyperlinks with DOM iterator v001.
#
# A usability example, separate from the DOM iterator. It builds a FRESH native
# Glon CLI into ./build from the repository's unchanged sources, and only reads
# ../glon-iter/iter.glon, ../glon-dom/dom.glon and ../glon-iter/measure.c.
#
#   make          build ./build/glon
#   make run      run example.glon on page.glon and print the links
#   make test     the regression tests (links-tests.glon)
#   make measure  bindings, loader, code, managed heap and return stack
#   make clean

ROOT    := ../..
CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g
RTSRC   := $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
SRCS    := $(ROOT)/glon.c $(RTSRC)
HDRS    := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h
GLON    := build/glon
LIBS    := build/glon-lib/prelude.glon build/glon-lib/strings.glon
# the load order: the string extension, the iterator, the DOM walker, then this example
USES    := $(ROOT)/demo/shop/strings-ext.glon ../glon-iter/iter.glon ../glon-dom/dom.glon list-block.glon links.glon page.glon
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

# The CLI prints #[func] for each wrapped library file; that line is filtered out.
run: all
	@$(GLON) $(USES) example.glon > build/run.out 2>&1 || { cat build/run.out; exit 1; }
	@grep -v '^#\[func\]$$' build/run.out

test: all
	@$(GLON) $(USES) links-tests.glon > build/test.out 2>&1 || { cat build/test.out; exit 1; }
	@grep -v '^#\[func\]$$' build/test.out

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

.PHONY: all run test measure clean
