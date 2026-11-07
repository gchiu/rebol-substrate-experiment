# experiments/glon-dom-host/Makefile -- the host-backed DOM walk in the browser
# (glon-live.wasm + host.js) and its native measurement harness (hostsim).
# Everything is built from the UNCHANGED runtime and GLON_LIVE dispatcher.
#
#   make wasm       build/glon-live.wasm (needs emcc: source ~/emsdk/emsdk_env.sh)
#   make hostsim    build/hostsim (native; the C stand-in host)
#   make test       hostsim on 100 / 1,000 / 10,000-node documents
#   make browser    run_browser.py: real Chromium (Python Playwright), checks
#                   Glon's links against a JavaScript reference, screenshot
#   make serve      serve the repository root on :8000; open
#                   http://127.0.0.1:8000/experiments/glon-dom-host/index.html

ROOT  := ../..
RT    := $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
EMCC  ?= emcc

all: hostsim pages

hostsim: build/hostsim
build/hostsim: hostsim.c $(RT)
	@mkdir -p build
	$(CC) -std=c17 -O1 -D_POSIX_C_SOURCE=200809L -DGLON_LIVE -I$(ROOT) -o $@ hostsim.c $(RT)

wasm: build/glon-live.wasm
build/glon-live.wasm: $(ROOT)/standalone/glon.c $(RT) $(ROOT)/r0_s1_show.c
	@mkdir -p build
	cd $(ROOT) && $(EMCC) -O1 -nostdlib -fno-builtin -s STANDALONE_WASM=1 -s ALLOW_MEMORY_GROWTH=1 \
	  -Wl,--no-entry -Wl,--export-memory -I. -DGLON_LIVE standalone/glon.c r0_s1_g1a_live.c \
	  r0_s1_g1a.c s1.c r0_s1_runtime.c r0_s1_show.c -o experiments/glon-dom-host/build/glon-live.wasm

pages:
	python3 gen_pages.py build/pages 100 1000 10000

test: hostsim
	@for b in 12 125 1250; do ./build/hostsim -q $$b 10 20 64 || exit 1; done

browser: pages
	python3 -u run_browser.py --screenshot build/demo.png

serve:
	cd $(ROOT) && python3 -m http.server 8000 --bind 127.0.0.1

.PHONY: all hostsim wasm pages test browser serve
