ROOT := ../..
CC ?= cc
CFLAGS ?= -std=c17 -O0 -g -Wall -Wextra
SRCS := $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
GLON := build/glon
LIBS := build/glon-lib/prelude.glon build/glon-lib/strings.glon
EXT := $(ROOT)/demo/shop/strings-ext.glon
REBOL ?= $(ROOT)/rebol-core/rebol

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

oracle:
	$(REBOL) -q r2-oracle.r
	$(REBOL) -q r2-stage2-oracle.r
	$(REBOL) -q r2-capture-oracle.r

test: all
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-a.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-b.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-c.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-d1.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-d2.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-e.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-f1.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-f2.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-g.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-h.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-i1.glon
	$(GLON) $(EXT) parse.glon parse-test-common.glon parse-tests-i2.glon

clean:
	rm -rf build
.PHONY: all test clean
