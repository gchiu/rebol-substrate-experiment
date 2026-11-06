# experiments/glon-latex/Makefile - GLON-LATEX v001.
#
# Builds a FRESH copy of the native Glon CLI into ./build from the repository's
# unchanged sources (the same GLON_SRCS as the root Makefile's `glon` target),
# with the canonical core libraries copied beside it in build/glon-lib/, where
# the CLI looks for them. Nothing outside this directory is written, and the
# repository's own ./glon binary is left alone.
#
#   make            build ./build/glon
#   make test       build it and run the LaTeX vocabulary tests
#   make examples   print a few rendered expressions
#   make clean

ROOT    := ../..
CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g
SRCS    := $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c \
           $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
HDRS    := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h
GLON    := build/glon
LIBS    := build/glon-lib/prelude.glon build/glon-lib/strings.glon

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
	$(GLON) latex.glon latex-tests.glon

examples: all
	$(GLON) latex.glon examples.glon

clean:
	rm -rf build

.PHONY: all test examples clean
