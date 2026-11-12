ROOT := ../..
CC ?= cc
CFLAGS ?= -std=c17 -O0 -g -Wall -Wextra
SRCS := $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
GLON := build/glon
LIBS := build/glon-lib/prelude.glon build/glon-lib/strings.glon
EXT := $(ROOT)/demo/shop/strings-ext.glon

all: $(GLON) $(LIBS)

$(GLON): $(SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(ROOT) -o $@ $(SRCS)

build/glon-lib/prelude.glon: $(ROOT)/jupyter/prelude.glon
	@mkdir -p build/glon-lib
	cp $< $@
build/glon-lib/strings.glon: $(ROOT)/demo/shop/strings.glon
	@mkdir -p build/glon-lib
	cp $< $@

test: all
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-a.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-b.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-c.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-d1.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-d2.glon

clean:
	rm -rf build
.PHONY: all test clean
