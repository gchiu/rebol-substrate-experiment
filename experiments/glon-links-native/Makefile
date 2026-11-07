# experiments/glon-links-native/Makefile -- glon-web: a native Glon host that
# fetches a URL (libcurl), parses it (libxml2) and lets a Glon program walk the
# document through handles. Builds from the UNCHANGED runtime and GLON_LIVE
# dispatcher; nothing outside this directory is written.
#
#   make            build/glon-web and build/glon-web-lib/
#   make test       run the offline tests (local pages through file:// URLs)
#   make measure    host-backed vs materialised, 100 / 1,000 / 10,000 elements
#   make demo URL=https://example.com
#   make clean
#
# libxml2 and libcurl: the installed runtime libraries are used. Their headers
# (and ICU's, which libxml2's include) come from the system (-dev packages) or,
# when those are not installed, from
# build/sysroot (`make headers` unpacks the matching Ubuntu -dev packages there
# with apt-get download; no root needed).

ROOT    := ../..
CC      ?= cc
SYSROOT := build/sysroot/usr/include
CFLAGS  ?= -std=c17 -O1 -Wall -Wextra
INC     := -I$(ROOT) -I/usr/include/libxml2 -I$(SYSROOT)/libxml2 -I$(SYSROOT)/x86_64-linux-gnu -I$(SYSROOT)
LIBDIR  := /usr/lib/x86_64-linux-gnu
LDLIBS  := $(LIBDIR)/libxml2.so.2 $(LIBDIR)/libcurl.so.4
RT      := $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
HDRS    := $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h $(ROOT)/r0_s1_g1a_live.h
LIB     := build/glon-web-lib
LIBSRC  := $(ROOT)/jupyter/prelude.glon ../glon-dom-host/emit.glon $(ROOT)/demo/shop/strings.glon \
           $(ROOT)/demo/shop/strings-ext.glon ../glon-iter/iter.glon ../glon-dom/dom.glon \
           ../glon-dom-links/list-block.glon ../glon-dom-links/links.glon ../glon-dom-host/hostwalk.glon
WEB     := build/glon-web

all: $(WEB) lib

$(WEB): glon-web.c $(RT) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -DGLON_LIVE $(INC) -o $@ glon-web.c $(RT) $(LDLIBS)

lib: $(LIBSRC)
	@mkdir -p $(LIB)
	cp $(LIBSRC) $(LIB)/

headers:
	mkdir -p build/deb && cd build/deb && apt-get download libxml2-dev libcurl4-openssl-dev libicu-dev
	for d in build/deb/*.deb; do dpkg-deb -x $$d build/sysroot; done

pages:
	python3 gen_pages.py build/pages 100 1000 10000

test: all pages
	sh test.sh

measure: all pages
	sh measure.sh

demo: all
	$(WEB) links.glon $(URL)

clean:
	rm -rf build/glon-web build/glon-web-lib build/pages build/*.out

.PHONY: all lib headers pages test measure demo clean
