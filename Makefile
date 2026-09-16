CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g

OBJS = s1.o tests.o adversarial.o claims.o r0.o r0_tests.o r0_s1_runtime.o r0_s1_tests.o r0_s1_debug_tests.o main.o

all: s1

s1: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

s1.o: s1.c s1.h
tests.o: tests.c s1.h
adversarial.o: adversarial.c s1.h
claims.o: claims.c s1.h
r0.o: r0.c r0.h s1.h
r0_tests.o: r0_tests.c r0.h s1.h
r0_s1_runtime.o: r0_s1_runtime.c r0_s1.h s1.h
r0_s1_tests.o: r0_s1_tests.c r0_s1.h s1.h
r0_s1_debug_tests.o: r0_s1_debug_tests.c r0_s1.h s1.h
main.o: main.c s1.h

test: s1
	./check-frozen-s1.sh
	./s1

clean:
	rm -f s1 $(OBJS)

# ---- WebAssembly browser demo (Emscripten) --------------------------------
# Produces web/demo.js (Emscripten runtime + web/glue.js) and web/demo.wasm,
# then generates the public GitHub Pages page docs/ (index.html + demo.js +
# demo.wasm) from the single authoritative R0 source web/demo.r0.
EMCC      ?= emcc
WASM_FLAGS = -O1 -s ALLOW_MEMORY_GROWTH=1 \
	-s EXPORT_KEEPALIVE=1 \
	--pre-js web/glue.js \
	--embed-file web/demo.r0@demo.r0 \
	-I.

wasm: web/demo.js docs/index.html

web/demo.js: web/demo.c web/demo.r0 web/glue.js s1.c s1.h r0_s1_runtime.c r0_s1.h
	$(EMCC) $(WASM_FLAGS) web/demo.c s1.c r0_s1_runtime.c -o web/demo.js

# generate the static source listings in docs/index.html from demo.r0/glue.js
# (no JavaScript fetches or renders the source)
docs/index.html: web/demo.r0 web/glue.js web/build-docs.py web/demo.js web/demo.wasm
	python3 web/build-docs.py

# headless verification under node (no browser/DOM required)
wasm-test: web/demo.js
	$(EMCC) -O1 -s ALLOW_MEMORY_GROWTH=1 --embed-file web/demo.r0@demo.r0 -I. \
		web/node_test.c s1.c r0_s1_runtime.c -o /tmp/opencode_wasm_test.js
	node /tmp/opencode_wasm_test.js | tee /tmp/opencode_wasm_test.out
	@grep -q 1000001 /tmp/opencode_wasm_test.out && \
	 grep -q 1000002 /tmp/opencode_wasm_test.out && \
	 grep -q 1000003 /tmp/opencode_wasm_test.out && \
	 grep -q WASM_DEMO_OK /tmp/opencode_wasm_test.out && \
	 echo "wasm-test: PASS (3 clicks -> counter 3, emitted 1000001/1000002/1000003)"

# ---- Standalone WebAssembly demo (no Emscripten JS runtime) ----------------
# Builds a raw standalone/glon.wasm with `-nostdlib` (no libc, no WASI, no
# virtual filesystem) plus a handwritten standalone/glon.js.  The R0/GLON
# source (standalone/app.glon) is NOT embedded in the binary; it is inlined
# into the generated docs/standalone.html by standalone/build-docs.py and
# passed to glon_load at runtime.
STANDALONE_FLAGS = -O1 -nostdlib -fno-builtin \
	-s STANDALONE_WASM=1 -s ALLOW_MEMORY_GROWTH=1 \
	-Wl,--no-entry -Wl,--export-memory \
	-I.

wasm-standalone: standalone/glon.wasm docs/standalone.html

standalone/glon.wasm: standalone/glon.c s1.c s1.h r0_s1_runtime.c r0_s1.h
	$(EMCC) $(STANDALONE_FLAGS) standalone/glon.c s1.c r0_s1_runtime.c \
		-o standalone/glon.wasm

# generate docs/standalone.html from app.glon/glon.js and publish artifacts
docs/standalone.html: standalone/app.glon standalone/glon.js standalone/build-docs.py standalone/glon.wasm
	python3 standalone/build-docs.py

# headless verification under node (no browser/DOM required)
wasm-standalone-test: standalone/glon.wasm
	node standalone/node_test.js | tee /tmp/opencode_glon_test.out
	@grep -q "GLON_TEST PASS" /tmp/opencode_glon_test.out && \
	 echo "wasm-standalone-test: PASS (3 clicks -> counter 3)"

.PHONY: all test clean wasm wasm-test wasm-standalone wasm-standalone-test
