CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g

OBJS = s1.o tests.o adversarial.o claims.o r0.o r0_tests.o r0_s1_runtime.o r0_s1_g1a.o r0_s1_tests.o r0_s1_debug_tests.o r0_s1_m1_tests.o r0_s1_m2_tests.o r0_s1_m3_tests.o r0_s1_m3b_tests.o r0_s1_m3c_tests.o r0_s1_m3d_tests.o r0_s1_nested_closure_tests.o r0_s1_p4_tests.o r0_s1_p5_tests.o r0_s1_g1a_tests.o r0_s1_g1b_tests.o r0_s1_g1c_tests.o r0_s1_g1d_tests.o r0_s1_g1e_tests.o r0_s1_lambda_tests.o r0_s1_masm_tests.o r0_s1_gc_safepoint_tests.o r0_s1_invoke_tests.o r0_s1_reduce_tests.o r0_s1_parse_tests.o r0_s1_equality_tests.o r0_s1_bound_tests.o r0_s1_traffic_tests.o r0_s1_newell_tests.o r0_s1_ovm_tests.o r0_s1_nasch_tests.o main.o

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
r0_s1_g1a.o: r0_s1_g1a.c r0_s1_g1a.h r0_s1.h s1.h
r0_s1_g1a_tests.o: r0_s1_g1a_tests.c r0_s1_g1a.h r0_s1.h s1.h
r0_s1_g1b_tests.o: r0_s1_g1b_tests.c r0_s1_g1a.h r0_s1.h s1.h
r0_s1_g1c_tests.o: r0_s1_g1c_tests.c r0_s1_g1a.h r0_s1.h s1.h
r0_s1_g1d_tests.o: r0_s1_g1d_tests.c r0_s1_g1a.h r0_s1.h s1.h
r0_s1_g1e_tests.o: r0_s1_g1e_tests.c r0_s1_g1a.h r0_s1.h s1.h
r0_s1_lambda_tests.o: r0_s1_lambda_tests.c r0_s1.h s1.h
r0_s1_masm_tests.o: r0_s1_masm_tests.c r0_s1.h s1.h
r0_s1_gc_safepoint_tests.o: r0_s1_gc_safepoint_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_invoke_tests.o: r0_s1_invoke_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_reduce_tests.o: r0_s1_reduce_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_parse_tests.o: r0_s1_parse_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_equality_tests.o: r0_s1_equality_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_bound_tests.o: r0_s1_bound_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_traffic_tests.o: r0_s1_traffic_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_newell_tests.o: r0_s1_newell_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_ovm_tests.o: r0_s1_ovm_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_nasch_tests.o: r0_s1_nasch_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_tests.o: r0_s1_tests.c r0_s1.h s1.h
r0_s1_debug_tests.o: r0_s1_debug_tests.c r0_s1.h s1.h
r0_s1_m1_tests.o: r0_s1_m1_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_m2_tests.o: r0_s1_m2_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_m3_tests.o: r0_s1_m3_tests.c r0_s1.h s1.h
r0_s1_m3b_tests.o: r0_s1_m3b_tests.c r0_s1.h r0_s1_m3c_lib.h s1.h
r0_s1_m3c_tests.o: r0_s1_m3c_tests.c r0_s1.h r0_s1_m3c_lib.h s1.h
r0_s1_m3d_tests.o: r0_s1_m3d_tests.c r0_s1.h r0_s1_m3c_lib.h r0_s1_m3d_lib.h s1.h
r0_s1_nested_closure_tests.o: r0_s1_nested_closure_tests.c r0_s1.h s1.h
r0_s1_p4_tests.o: r0_s1_p4_tests.c r0_s1.h s1.h
r0_s1_p5_tests.o: r0_s1_p5_tests.c r0_s1.h s1.h
main.o: main.c s1.h

test: s1
	./check-frozen-s1.sh
	./s1

# ---- FIB-PROFILE-P1: Fibonacci profiler (instrumented + baseline builds) ---
# fib-profiler : counters + trace (compiles runtime AND driver with -DR0_S1_PROFILE)
# fib-timing   : baseline timing only (no instrumentation, zero added overhead)
PROFILE_FLAGS = $(CFLAGS) -DR0_S1_PROFILE

fib-profiler: r0_s1_fib_profiler_prof.o r0_s1_runtime_prof.o s1.o
	$(CC) $(PROFILE_FLAGS) -o $@ r0_s1_fib_profiler_prof.o r0_s1_runtime_prof.o s1.o

fib-timing: r0_s1_fib_profiler.o r0_s1_runtime.o s1.o
	$(CC) $(CFLAGS) -o $@ r0_s1_fib_profiler.o r0_s1_runtime.o s1.o

r0_s1_fib_profiler.o: r0_s1_fib_profiler.c r0_s1.h s1.h
	$(CC) $(CFLAGS) -c -o $@ r0_s1_fib_profiler.c

r0_s1_fib_profiler_prof.o: r0_s1_fib_profiler.c r0_s1.h s1.h
	$(CC) $(PROFILE_FLAGS) -c -o $@ r0_s1_fib_profiler.c

r0_s1_runtime_prof.o: r0_s1_runtime.c r0_s1.h s1.h
	$(CC) $(PROFILE_FLAGS) -c -o $@ r0_s1_runtime.c

# ---- FIB-OPT-P6: diagnostic profiler (S1 opcode/HOST event counters) ----
# Links an instrumented COPY of the S1 machine (s1_prof.c); the frozen s1.c is
# untouched and the ordinary `s1` binary is unaffected.
fib-p6-prof: r0_s1_p6_prof.o s1_prof.o r0_s1_runtime_prof.o
	$(CC) $(PROFILE_FLAGS) -o $@ r0_s1_p6_prof.o s1_prof.o r0_s1_runtime_prof.o

s1_prof.o: s1_prof.c s1.h s1_prof.h
	$(CC) $(CFLAGS) -c -o $@ s1_prof.c

r0_s1_p6_prof.o: r0_s1_p6_prof.c r0_s1.h s1.h s1_prof.h
	$(CC) $(PROFILE_FLAGS) -c -o $@ r0_s1_p6_prof.c

# ---- FIB-OPT-P6B: compiler-optimisation control (one variable: -O level) ----
# Non-counting runtime. Build once per level, e.g.:
#   make CFLAGS="-std=c17 -Wall -Wextra -O2" fib-p6b-bench
fib-p6b-bench: r0_s1_p6b_bench.c r0_s1_runtime.c s1.c
	$(CC) $(CFLAGS) -o $@ r0_s1_p6b_bench.c r0_s1_runtime.c s1.c

# ---- FIB-OPT-P10A: compiled-S1 baseline (mechanical S1 -> C, then -O2) ----
# Generates + compiles the S1 stream at runtime and compares interpreted vs
# compiled fib 25. Needs libdl. The generated C is gcc -O2 -shared -fPIC.
fib-p10a-bench: r0_s1_p10a_bench.c r0_s1_runtime.c s1.c
	$(CC) $(CFLAGS) -o $@ r0_s1_p10a_bench.c r0_s1_runtime.c s1.c -ldl

# ---- FIB-OPT-P10B..P10F: compiled-S1 variants ----
# P10B (promoted IP/SP/RP), P10C (+ inline pure HOST), P10D (+ single TOS
# cache), P10E (+ NOS cache), P10F (+ sequential fall-through). Generates +
# compiles the S1 stream at runtime and compares. libdl.
fib-p10b-bench: r0_s1_p10b_bench.c r0_s1_runtime.c s1.c
	$(CC) $(CFLAGS) -o $@ r0_s1_p10b_bench.c r0_s1_runtime.c s1.c -ldl

clean:
	rm -f s1 fib-profiler fib-timing fib-p6b-bench fib-p10a-bench fib-p10b-bench traffic-bench-o0 traffic-bench-o2 $(OBJS) r0_s1_fib_profiler.o r0_s1_fib_profiler_prof.o r0_s1_runtime_prof.o

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

# ---- G1A: browser-hosted Glon/WASM application skeleton --------------------
# Builds demo/shop/glon.wasm (the standalone WASM runtime + the G1A template
# engine + routing primitive) and bundles the application into
# demo/shop/app.html (fragments inlined as GLON byte-lists).  No Emscripten JS
# runtime, no libc, no WASI, no virtual filesystem.
wasm-g1a: demo/shop/glon.wasm demo/shop/app.html

demo/shop/glon.wasm: standalone/glon.c r0_s1_g1a.c r0_s1_g1a.h s1.c s1.h r0_s1_runtime.c r0_s1.h
	$(EMCC) $(STANDALONE_FLAGS) standalone/glon.c r0_s1_g1a.c s1.c r0_s1_runtime.c \
		-o demo/shop/glon.wasm

# bundle the bootstrap (common.glon + app.glon) into demo/shop/app.html
# (+ demo/shop/bootstrap.glon for tests). Demos stay separate (demos/*.glon).
demo/shop/app.html: demo/shop/common.glon demo/shop/app.glon demo/shop/build.py
	python3 demo/shop/build.py

# headless verification under node (the real WASM module + host bridge)
wasm-g1a-test: demo/shop/glon.wasm demo/shop/app.html
	node demo/shop/node_test.js | tee /tmp/opencode_g1a_test.out
	@grep -q "GLON_G1E_TEST PASS" /tmp/opencode_g1a_test.out && \
	 echo "wasm-g1a-test: PASS (home / products / add-product x3 / search / persist / unknown)"

# ---- Standalone traffic page (post-Alpha application; no language change) ---
# Bundles common.glon + traffic.glon into demo/shop/traffic.html (no launcher),
# and verifies the wiring headlessly against the real WASM + event bridge.
traffic.html: demo/shop/common.glon demo/shop/demos/traffic.glon demo/shop/build-traffic.py
	python3 demo/shop/build-traffic.py

wasm-traffic-test: demo/shop/glon.wasm traffic.html
	node demo/shop/traffic_node_test.js | tee /tmp/opencode_traffic_test.out
	@grep -q "TRAFFIC_TEST PASS" /tmp/opencode_traffic_test.out && \
	 echo "wasm-traffic-test: PASS (embedded source / initial render / advance / disturbance / reset)"

# ---- Traffic simulator benchmark (post-Alpha application; no language change) -
# load-once benchmark of the IDM sim. Builds both the shipped -O0 runtime and a
# directly-compiled -O2 runtime so interpreter cost can be separated from C
# optimisation level. Not part of the test suite.
traffic-bench: traffic-bench-o0 traffic-bench-o2

traffic-bench-o0: r0_s1_traffic_bench.c r0_s1_runtime.o s1.o
	$(CC) $(CFLAGS) -o $@ r0_s1_traffic_bench.c r0_s1_runtime.o s1.o

traffic-bench-o2: r0_s1_traffic_bench.c r0_s1_runtime.c s1.c
	$(CC) -std=c17 -O2 -o $@ r0_s1_traffic_bench.c r0_s1_runtime.c s1.c

.PHONY: all test clean wasm wasm-test wasm-standalone wasm-standalone-test wasm-g1a wasm-g1a-test traffic-bench traffic-bench-o0 traffic-bench-o2 wasm-traffic-test
