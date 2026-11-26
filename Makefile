CC      ?= cc
CFLAGS  ?= -std=c17 -fwrapv -Wall -Wextra -O0 -g

OBJS = s1.o tests.o adversarial.o claims.o r0.o r0_tests.o r0_s1_runtime.o r0_s1_g1a.o r0_s1_tests.o r0_s1_debug_tests.o r0_s1_m1_tests.o r0_s1_m2_tests.o r0_s1_m3_tests.o r0_s1_m3b_tests.o r0_s1_m3c_tests.o r0_s1_m3d_tests.o r0_s1_nested_closure_tests.o r0_s1_p4_tests.o r0_s1_p5_tests.o r0_s1_g1a_tests.o r0_s1_g1b_tests.o r0_s1_g1c_tests.o r0_s1_g1d_tests.o r0_s1_g1e_tests.o r0_s1_lambda_tests.o r0_s1_masm_tests.o r0_s1_gc_safepoint_tests.o r0_s1_invoke_tests.o r0_s1_reduce_tests.o r0_s1_parse_tests.o r0_s1_equality_tests.o r0_s1_bound_tests.o r0_s1_case_tests.o r0_s1_escape_law_tests.o r0_s1_error_tests.o r0_s1_show.o r0_s1_primer_tests.o r0_s1_parse_hardening_tests.o r0_s1_context_capacity_tests.o r0_s1_gc_worklist_tests.o r0_s1_gc_interior_tests.o r0_s1_masm_bound_tests.o r0_s1_session.o r0_s1_session_tests.o r0_s1_string_ops_tests.o r0_s1_string_ext_tests.o r0_s1_traffic_tests.o r0_s1_newell_tests.o r0_s1_ovm_tests.o r0_s1_nasch_tests.o main.o

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
r0_s1_case_tests.o: r0_s1_case_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_escape_law_tests.o: r0_s1_escape_law_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_error_tests.o: r0_s1_error_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_show.o: r0_s1_show.c r0_s1.h s1.h
r0_s1_primer_tests.o: r0_s1_primer_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_parse_hardening_tests.o: r0_s1_parse_hardening_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_context_capacity_tests.o: r0_s1_context_capacity_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_gc_worklist_tests.o: r0_s1_gc_worklist_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_gc_interior_tests.o: r0_s1_gc_interior_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_masm_bound_tests.o: r0_s1_masm_bound_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_session.o: r0_s1_session.c r0_s1.h s1.h
r0_s1_session_tests.o: r0_s1_session_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_string_ops_tests.o: r0_s1_string_ops_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_string_ext_tests.o: r0_s1_string_ext_tests.c r0_s1.h m1_layout.h s1.h
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

test: s1 gc-worklist-overflow-test
	./check-frozen-s1.sh
	./s1
	./gc-worklist-overflow-test
	python3 demo/shop/build-primer.py --check

# GC worklist overflow: a SEPARATE enlarged-heap TEST build (never shipped),
# identified by R0S1_GC_OVERFLOW_TEST_BUILD, the only switch that enlarges the
# managed heap (to 11,232 cells, fixed in r0_s1.h) and skips the build-time
# worklist assertion, so the runtime bound (R0S1_HALT_GC_WORKLIST_FULL) can be
# exercised. It compiles its own copy of the runtime; no other target defines
# the switch. The shipped build keeps the 64K map and the assertion.
GC_OVERFLOW_TEST_FLAGS = -DR0S1_GC_OVERFLOW_TEST_BUILD
gc-worklist-overflow-test: gc_worklist_overflow_test.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c s1.c r0_s1.h s1.h m1_layout.h
	$(CC) $(CFLAGS) $(GC_OVERFLOW_TEST_FLAGS) -I. -o $@ gc_worklist_overflow_test.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c s1.c

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
	rm -f s1 fib-profiler fib-timing fib-p6b-bench fib-p10a-bench fib-p10b-bench traffic-bench-o0 traffic-bench-o2 glon-live-native-test demo/shop/glon-live.wasm $(OBJS) r0_s1_fib_profiler.o r0_s1_fib_profiler_prof.o r0_s1_runtime_prof.o
	rm -f glon-desktop glon-desktop.exe desktop-test patrol-test kaka-test kaka-render-test kaka-life-test kaka-damage-test kaka-terminal-test kaka-wave-test desktop/glon.wasm desktop/prelude.glon desktop/strings.glon desktop/big.txt
	rm -rf glon glon-lib
	rm -f .glon-config .glon-config.tmp io-file-c-test sec-test sqlite-test arch-check m1-smoke m1-smoke.exe

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

wasm: web/demo.js docs/r0-counter.html

web/demo.js: web/demo.c web/demo.r0 web/glue.js s1.c s1.h r0_s1_runtime.c r0_s1.h
	$(EMCC) $(WASM_FLAGS) web/demo.c s1.c r0_s1_runtime.c -o web/demo.js

# generate the static source listings in docs/r0-counter.html from demo.r0/glue.js
# (no JavaScript fetches or renders the source). The site root, docs/index.html,
# is the Glon landing page: site/index.html, published by the Pages workflow.
docs/r0-counter.html: web/demo.r0 web/glue.js web/build-docs.py web/demo.js web/demo.wasm
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

standalone/glon.wasm: standalone/glon.c s1.c s1.h r0_s1_runtime.c r0_s1_show.c r0_s1.h
	$(EMCC) $(STANDALONE_FLAGS) standalone/glon.c s1.c r0_s1_runtime.c r0_s1_show.c \
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

demo/shop/glon.wasm: standalone/glon.c r0_s1_g1a.c r0_s1_g1a.h s1.c s1.h r0_s1_runtime.c r0_s1_show.c r0_s1.h
	$(EMCC) $(STANDALONE_FLAGS) standalone/glon.c r0_s1_g1a.c s1.c r0_s1_runtime.c r0_s1_show.c \
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

# ---- Standalone Linda page (post-Alpha application; no language change) -----
# Bundles the self-contained demos/linda.glon into demo/shop/linda.html and
# verifies the cooperative blocking/wakeup wiring headlessly against the real
# WASM + event bridge.
linda.html: demo/shop/demos/linda.glon demo/shop/build-linda.py
	python3 demo/shop/build-linda.py

wasm-linda-test: demo/shop/glon.wasm linda.html
	node demo/shop/linda_node_test.js | tee /tmp/opencode_linda_test.out
	@grep -q "LINDA_TEST PASS" /tmp/opencode_linda_test.out && \
	 echo "wasm-linda-test: PASS (embedded source / reset / A blocks at IN / B runs while A blocked / OUT wakes A / A resumes after IN / all finish / deterministic)"

# ---- WASM regression: closure-origin binding law + CASE vocabulary ----------
# The G1E/traffic/Linda/web suites do not exercise CASE or the closure-origin
# binding law, so this probe loads bootstrap.glon + case.glon into the real
# demo/shop/glon.wasm and runs the native r0_s1_case_tests.c scenarios.
wasm-binding-test: demo/shop/glon.wasm
	node demo/shop/binding_case_wasm_test.js | tee /tmp/opencode_binding_test.out
	@grep -q "BINDING_CASE_WASM_TEST PASS" /tmp/opencode_binding_test.out && \
	 echo "wasm-binding-test: PASS (closure-origin binding law + CASE on real WASM)"

# ---- Generic WASM host-ABI arena regression ---------------------------------
# Sustained host->WASM event traffic through the generic ABI: >=10k iterations
# of glon_alloc(token) + glon_alloc(value) + glon_event_value, plus >=10k
# value-less glon_event calls.  Verifies zero allocation failures, no token/value
# aliasing, correct dispatch throughout, and that a large argument buffer still
# fits.  Independent of any single application (D11.6).
wasm-abi-test: demo/shop/glon.wasm
	node demo/shop/abi_arena_wasm_test.js | tee /tmp/opencode_abi_test.out
	@grep -q "ABI_ARENA_WASM_TEST PASS" /tmp/opencode_abi_test.out && \
	 echo "wasm-abi-test: PASS (10k valued + 10k value-less events, no alloc failure/aliasing)"

# ---- Experimental GLON_LIVE host boundary (separate WASM + page) -----------
# A separate -DGLON_LIVE build with a fifth host import (host_call) and a new
# glon_event_bytes export.  Existing shop/browser builds keep the four-import
# ABI and are untouched.  Requires emcc + node (CI); the native live test
# (below) covers the C boundary without them.
demo/shop/glon-live.wasm: standalone/glon.c r0_s1_g1a_live.c r0_s1_g1a_live.h r0_s1_g1a.c r0_s1_g1a.h s1.c s1.h r0_s1_runtime.c r0_s1_show.c r0_s1.h
	$(EMCC) $(STANDALONE_FLAGS) -DGLON_LIVE standalone/glon.c r0_s1_g1a_live.c r0_s1_g1a.c s1.c r0_s1_runtime.c r0_s1_show.c -o $@

demo/shop/live.html: demo/shop/common.glon demo/shop/strings.glon demo/shop/live.glon demo/shop/live-host.js demo/shop/qwen-client.js demo/shop/build-live.py
	python3 demo/shop/build-live.py

wasm-live-test: demo/shop/glon-live.wasm demo/shop/live.html
	node demo/shop/qwen_client_test.js | tee /tmp/opencode_qwen_client_test.out
	@grep -q "QWEN_CLIENT_TEST PASS" /tmp/opencode_qwen_client_test.out
	node demo/shop/live_node_test.js | tee /tmp/opencode_live_test.out
	@grep -q "LIVE_TEST PASS" /tmp/opencode_live_test.out && \
	 echo "wasm-live-test: PASS (Qwen protocol mapping + Glon capture console + glon_event_bytes)"

# ---- Glon primer (newcomer onboarding; no language change) -----------------
# demo/shop/primer.txt is the single source of the primer. build-primer.py
# renders it into GLON-PRIMER.md and demo/shop/primer.html (`--check`, run by
# `make test`, fails if either is stale); the native primer doc-tests run every
# example, and this probe runs every example through the real WASM glon_run.
primer.html: demo/shop/primer.txt demo/shop/build-primer.py demo/shop/bootstrap.glon demo/shop/case.glon demo/shop/demos/tuple-space.glon
	python3 demo/shop/build-primer.py

wasm-primer-test: demo/shop/glon.wasm primer.html
	node demo/shop/primer_node_test.js | tee /tmp/opencode_primer_test.out
	@grep -q "PRIMER_TEST PASS" /tmp/opencode_primer_test.out && \
	 echo "wasm-primer-test: PASS (every primer example through glon_run on real WASM)"

# ---- Native Glon command-line runner (distributable Linux ELF) --------------
# A thin host over the persistent session facade (r0_s1_session_run_block):
# one runtime, the shipped core libraries, then every FILE in one session.
# glon-lib/ is copied beside the executable; it is not compiled in.
# ---- Configurable memory profile -------------------------------------------
# constrained: the frozen 64K reference (MEM_CELLS 65536, 7,232-cell managed
#              heap, loader and M1 task arena unchanged).  This is the default
#              and the conformance profile.
# large:       a generated S1 object with a larger MEM_CELLS, an enlarged
#              managed heap, an independent GC worklist and loader heap, and
#              the M1 arena/table relocated above them.  Frozen s1.c/s1.h are
#              read only; the generated copies differ only in MEM_CELLS.
MEM_PROFILE ?= constrained
MEM_MANAGED_CELLS ?= 1048576
MEM_WORKLIST_CELLS ?= 40000
MEM_LOADER_CELLS ?= 200000
MEM_M1_CELLS ?= 10200
MEM_TOTAL ?= 65536
CODE_RESERVE ?= 256
MEM_CODE_CAP ?= 16384
GLON_MEMORY_CFLAGS :=
GLON_PROFILE_DEPS :=
GLON_S1_SRC := s1.c
ifeq ($(MEM_PROFILE),constrained)
else ifeq ($(MEM_PROFILE),large)
  MEM_SHIFT := $(shell expr $(MEM_CODE_CAP) - 16384)
  MEM_RS_INIT := $(shell expr 24576 + $(MEM_SHIFT))
  MEM_HEAP_BASE := $(shell expr 32768 + $(MEM_SHIFT))
  MEM_MANAGED_END := $(shell expr $(MEM_HEAP_BASE) + $(MEM_MANAGED_CELLS))
  MEM_LOAD_BASE := $(shell expr $(MEM_MANAGED_END) + $(MEM_WORKLIST_CELLS))
  MEM_M1_BASE := $(shell expr $(MEM_LOAD_BASE) + $(MEM_LOADER_CELLS))
  MEM_TASK_TABLE := $(shell expr $(MEM_M1_BASE) + $(MEM_M1_CELLS))
  MEM_TOTAL := $(shell expr $(MEM_TASK_TABLE) + 536 + 1024)
  GLON_MEMORY_CFLAGS := -DR0S1_DS_INIT=$(MEM_CODE_CAP) \
      -DR0S1_RS_INIT=$(MEM_RS_INIT) \
      -DR0S1_HEAP_BASE=$(MEM_MANAGED_END) \
      -DR0S1_LOAD_BASE=$(MEM_LOAD_BASE) \
      -DR0S1_HEAP_LIMIT=$(MEM_M1_BASE) \
      -DGC_HEAP_BASE=$(MEM_HEAP_BASE) \
      -DGC_WORKLIST=$(MEM_MANAGED_END) \
      -DGC_WORKLIST_CAP=$(MEM_WORKLIST_CELLS) \
      -DM1_ARENA_BASE=$(MEM_M1_BASE) \
      -DM1_TASK_TABLE=$(MEM_TASK_TABLE) \
      -DR0S1_DS_RESERVE=$(CODE_RESERVE)
  GLON_S1_SRC := build/profile/s1_profile.c
  GLON_PROFILE_DEPS := build/profile/s1_profile.c build/profile/s1_profile.h
else
  $(error unknown MEM_PROFILE='$(MEM_PROFILE)'; supported values: constrained, large)
endif

GLON_SRCS = glon.c r0_s1_session.c r0_s1_show.c r0_s1_runtime.c $(GLON_S1_SRC)
GLON_LIB_DIR = glon-lib
GLON_LIBS = $(GLON_LIB_DIR)/prelude.glon $(GLON_LIB_DIR)/strings.glon $(GLON_LIB_DIR)/strings-ext.glon

# Optional modules are compile-time selected. MODULES is empty for the
# unchanged minimal CLI.  The list is comma-separated:
#
#   MODULES=file        host-mediated async file module
#   MODULES=http        optional CivetWeb HTTPS listener + ordinary http.glon
#   MODULES=sec         optional OpenSSL-backed security primitives + sec.glon
#   MODULES=sqlite      optional SQLite database/statement vocabulary
#   MODULES=task        loadable cooperative multitasking (task.glon) + host pump
#   MODULES=file,http,sec,sqlite,task   additive selection
#
# Unknown names fail at build time.  Modules are additive: a minimal build
# never compiles or links any module object, source or Glon library.
comma := ,
GLON_MODULE_LIST := $(subst $(comma), ,$(strip $(MODULES)))
GLON_MODULE_UNKNOWN := $(filter-out file http sec sqlite task,$(GLON_MODULE_LIST))
ifneq ($(strip $(GLON_MODULE_UNKNOWN)),)
  $(error unknown MODULES='$(MODULES)'; supported values: file, http, sec, sqlite, task and combinations)
endif

GLON_MODULE_SRCS :=
GLON_MODULE_OBJS :=
GLON_MODULE_CFLAGS :=
GLON_MODULE_LIBS :=
GLON_MODULE_LDLIBS :=

ifneq ($(filter file,$(GLON_MODULE_LIST)),)
  GLON_MODULE_SRCS += glon_io_file.c
  GLON_MODULE_CFLAGS += -DGLON_MODULE_FILE
  GLON_MODULE_LIBS += $(GLON_LIB_DIR)/file.glon $(GLON_LIB_DIR)/file-stream.glon
  ifeq ($(OS),Windows_NT)
    GLON_MODULE_SRCS += glon_io_file_windows.c glon_io_file_cli_windows.c
  else
    GLON_MODULE_SRCS += glon_io_file_posix.c glon_io_file_cli.c
    GLON_MODULE_LDLIBS += -pthread
  endif
endif

# OpenSSL 3 development files are the only non-standard build dependency of
# the optional HTTP and security modules.  A normal Linux install provides them
# with `libssl-dev`; OPENSSL_CFLAGS/OPENSSL_LIBS let a local or cross
# environment point at an unpacked development package without editing the
# Makefile.
OPENSSL_CFLAGS ?=
OPENSSL_LIBS ?= -lssl -lcrypto

# SQLite is linked from the standard library; use SQLITE_CFLAGS/SQLITE_LIBS
# when the development headers/library are not in the default toolchain path.
SQLITE_CFLAGS ?=
SQLITE_LIBS ?= -lsqlite3

ifneq ($(filter sec,$(GLON_MODULE_LIST)),)
  GLON_MODULE_SRCS += glon_sec.c
  GLON_MODULE_CFLAGS += -DGLON_MODULE_SEC $(OPENSSL_CFLAGS)
  GLON_MODULE_LIBS += $(GLON_LIB_DIR)/sec.glon
  GLON_MODULE_LDLIBS += $(OPENSSL_LIBS)
endif

ifneq ($(filter sqlite,$(GLON_MODULE_LIST)),)
  GLON_MODULE_SRCS += glon_sqlite.c
  GLON_MODULE_CFLAGS += -DGLON_MODULE_SQLITE $(SQLITE_CFLAGS)
  GLON_MODULE_LIBS += $(GLON_LIB_DIR)/sqlite.glon
  GLON_MODULE_LDLIBS += $(SQLITE_LIBS)
  # The SQLite module reuses the file backend's canonical path confinement.
  # Compile that small backend when the file module itself is not selected.
  ifeq ($(filter file,$(GLON_MODULE_LIST)),)
    ifeq ($(OS),Windows_NT)
      GLON_MODULE_SRCS += glon_io_file_windows.c
    else
      GLON_MODULE_SRCS += glon_io_file_posix.c
    endif
  endif
endif

# ---- M2: loadable cooperative multitasking ----------------------------------
# The task module makes the M1 scheduler an ordinary Glon vocabulary
# (glon-lib/task.glon) and adds a host pump that alternates `run-tasks` with the
# async file/SQLite drains, so a task can suspend on I/O and be resumed.  The
# per-task stack budget is configurable; the defaults fit ten tasks in the
# 10,200-cell M1 arena (10 x 1,000).  The frozen 64K conformance build keeps the
# original 3-task defaults because it does not select this module.
M1_TASKS        ?= 10
M1_TASK_CELLS   ?= 1000
M1_DS_OFF       ?= 200
M1_RS_OFF       ?= 1000
M1_WRAPPER_DELTA ?= 256
ifneq ($(filter task,$(GLON_MODULE_LIST)),)
  GLON_MODULE_CFLAGS += -DGLON_MODULE_TASK -DM1_MAX_TASKS=$(M1_TASKS) \
      -DM1_TASK_CELLS=$(M1_TASK_CELLS) -DM1_DS_OFF=$(M1_DS_OFF) \
      -DM1_RS_OFF=$(M1_RS_OFF) -DM1_WRAPPER_DELTA=$(M1_WRAPPER_DELTA)
  GLON_MODULE_LIBS += $(GLON_LIB_DIR)/task.glon
endif

# The HTTP server does not create M1 tasks.  The G3 application plus the
# transient per-request source expression outgrow the default 14,800-cell
# loader heap, so the documented R0S1_HEAP_LIMIT override can reclaim the
# otherwise-unused M1 task arena for this build.  This is opt-in and changes
# no frozen S1 file; the minimal and file-only builds keep the default map.
GLON_HTTP_RECLAIM_M1 ?= 0
ifeq ($(GLON_HTTP_RECLAIM_M1),1)
  GLON_MODULE_CFLAGS += -DR0S1_HEAP_LIMIT=65000
endif

ifneq ($(filter http,$(GLON_MODULE_LIST)),)
  GLON_MODULE_SRCS += glon_http.c
  GLON_MODULE_OBJS += third_party/civetweb/civetweb.o
  GLON_MODULE_CFLAGS += -DGLON_MODULE_HTTP -Ithird_party/civetweb $(OPENSSL_CFLAGS)
  GLON_MODULE_LIBS += $(GLON_LIB_DIR)/http.glon
  GLON_MODULE_LDLIBS += -pthread $(OPENSSL_LIBS)
  ifeq ($(OS),Windows_NT)
    GLON_MODULE_LDLIBS += -lws2_32
  endif
endif

GLON_TARGET_LIBS = $(GLON_LIBS) $(GLON_MODULE_LIBS)

# CivetWeb is compiled separately with its own production flags, so the
# default -O0 -g CFLAGS and warnings used for Glon do not slow down or clutter
# the networking library.  TLS comes from the system OpenSSL 3.
third_party/civetweb/civetweb.o: third_party/civetweb/civetweb.c third_party/civetweb/civetweb.h \
		$(wildcard third_party/civetweb/*.inl)
	$(CC) -std=c17 -O2 -w -pthread -DOPENSSL_API_3_0 -DNO_CGI -DNO_CACHING \
		-DNO_SSL_DL -Ithird_party/civetweb $(OPENSSL_CFLAGS) -c $< -o $@

# Track the selected module configuration so `make glon` and
# `make glon MODULES=...` relink when the selection changes.
GLON_CONFIG = .glon-config
.PHONY: FORCE
FORCE:
$(GLON_CONFIG): FORCE
	@printf '%s %s %s %s %s %s %s %s %s %s %s %s' '$(strip $(MODULES))' '$(GLON_HTTP_RECLAIM_M1)' \
	    '$(MEM_PROFILE)' '$(MEM_MANAGED_CELLS)' '$(MEM_WORKLIST_CELLS)' '$(MEM_LOADER_CELLS)' \
	    '$(M1_TASKS)' '$(M1_TASK_CELLS)' '$(M1_DS_OFF)' '$(M1_RS_OFF)' '$(M1_WRAPPER_DELTA)' '$(MEM_M1_CELLS)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi

build/profile:
	@mkdir -p $@

build/profile/s1_profile.h: s1.h Makefile $(GLON_CONFIG) | build/profile
	@sed 's/^#define MEM_CELLS 65536$$/#define MEM_CELLS $(MEM_TOTAL)/' $< > $@.tmp
	@mv $@.tmp $@

build/profile/s1_profile.c: s1.c Makefile $(GLON_CONFIG) | build/profile
	@sed -e 's/^#include "s1.h"$$/#include "s1_profile.h"/' \
	      -e 's/^#define DS_INIT  *16384$$/#define DS_INIT    $(MEM_CODE_CAP)/' \
	      -e 's/^#define RS_INIT  *24576$$/#define RS_INIT    $(MEM_RS_INIT)/' \
	      -e 's/^#define HEAP_BASE  *32768$$/#define HEAP_BASE  $(MEM_HEAP_BASE)/' \
	      $< > $@.tmp
	@mv $@.tmp $@

glon: $(GLON_SRCS) $(GLON_MODULE_SRCS) $(GLON_MODULE_OBJS) $(GLON_PROFILE_DEPS) r0_s1.h s1.h m1_layout.h $(GLON_CONFIG) $(GLON_TARGET_LIBS)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) $(GLON_MODULE_CFLAGS) -I. -o $@ $(GLON_SRCS) $(GLON_MODULE_SRCS) $(GLON_MODULE_OBJS) $(GLON_MODULE_LDLIBS)

$(GLON_LIB_DIR)/prelude.glon: jupyter/prelude.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/strings.glon: demo/shop/strings.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

# strings-ext.glon is distributed beside the core libraries but NOT loaded by
# default (glon.c loads prelude + strings): load it explicitly when needed.
$(GLON_LIB_DIR)/strings-ext.glon: demo/shop/strings-ext.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/file.glon: modules/file.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/file-stream.glon: modules/file-stream.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/http.glon: modules/http.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/sec.glon: modules/sec.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/sqlite.glon: modules/sqlite.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/task.glon: modules/task.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

glon-lib: $(GLON_LIBS)

glon-smoke: glon
	./glon-smoke.sh

# Focused module tests: no R0/S1 runtime, so sanitizer builds are practical.
io-file-c-test: glon_io_file_tests.c glon_io_file.c glon_io_file_posix.c glon_io_file_cli.c glon_io_file.h glon_io_file_cli.h
	$(CC) $(CFLAGS) -I. -o $@ glon_io_file_tests.c glon_io_file.c glon_io_file_posix.c glon_io_file_cli.c -pthread
	./io-file-c-test

sqlite-test: tests/sqlite-g3d/sqlite_tests.c glon_sqlite.c glon_sqlite.h glon_io_file_posix.c glon_io_file.h
	$(CC) $(CFLAGS) $(SQLITE_CFLAGS) -I. -o sqlite-test tests/sqlite-g3d/sqlite_tests.c glon_sqlite.c glon_io_file_posix.c $(SQLITE_LIBS)
	rm -rf /tmp/glon-sqlite-c-test
	mkdir -p /tmp/glon-sqlite-c-test
	./sqlite-test /tmp/glon-sqlite-c-test
	rm -rf /tmp/glon-sqlite-c-test

sec-test: tests/security-g3c/sec_tests.c glon_sec.c glon_sec.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -I. -o sec-test tests/security-g3c/sec_tests.c glon_sec.c $(OPENSSL_LIBS)
	./sec-test

# Optional module acceptance: build minimal, preserve it, build MODULES=file,
# then run the real CLI against both.  This target is not part of `make test`.
io-file-test: io-file-c-test
	@set -e; tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
	$(MAKE) --no-print-directory glon MODULES=; \
	cp glon "$$tmp/glon-minimal"; \
	cp -R glon-lib "$$tmp/glon-lib"; \
	$(MAKE) --no-print-directory glon MODULES=file; \
	python3 tests/io-file/acceptance.py --glon ./glon --minimal "$$tmp/glon-minimal"

# Windows has no POSIX C test header/APIs; the same public vocabulary acceptance
# is run against the native file backend, plus a minimal build proving no file
# vocabulary leaks into MODULES=.
io-file-windows-test:
	@set -e; tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
	$(MAKE) --no-print-directory glon MODULES=; \
	cp glon.exe "$$tmp/glon-minimal.exe"; \
	cp -R glon-lib "$$tmp/glon-lib"; \
	$(MAKE) --no-print-directory glon MODULES=file; \
	python3 tests/io-file/acceptance.py --glon ./glon.exe --minimal "$$tmp/glon-minimal.exe"

# Streaming acceptance: 1 KiB through 64 KiB by default; --scale adds 1 MiB.
io-file-stream-test:
	$(MAKE) --no-print-directory glon MODULES=file
	python3 tests/io-file/streaming_acceptance.py --glon ./glon

io-file-stream-scale:
	$(MAKE) --no-print-directory glon MODULES=file
	python3 tests/io-file/streaming_acceptance.py --glon ./glon --scale

io-file-stream-4m:
	$(MAKE) --no-print-directory glon MODULES=file
	python3 tests/io-file/streaming_acceptance.py --glon ./glon --scale --four-mib

# Original-path replacement: unique same-directory temp, streaming write, atomic
# rename, source-change detection and cleanup.  Expected bytes come from Python.
io-file-inplace-test: io-file-c-test
	$(MAKE) --no-print-directory glon MODULES=file
	python3 tests/io-file/inplace_replacement.py --glon ./glon

io-file-inplace-bench:
	$(MAKE) --no-print-directory glon MODULES=file
	python3 tests/io-file/inplace_bench.py --glon ./glon

# ---- Optional HTTPS module (G1) --------------------------------------------
# Build the minimal CLI, the file module, the HTTP module and the combined
# module set, then run the bounded local HTTPS acceptance tests.  Requires
# OpenSSL 3 development files (libssl-dev) and the openssl command-line tool.
https-g1-test:
	$(MAKE) --no-print-directory glon MODULES=
	@./glon tests/https-g1/minimal-smoke.glon | grep -qx minimal-ok
	$(MAKE) --no-print-directory glon MODULES=file
	$(MAKE) --no-print-directory glon MODULES=http
	tests/https-g1/make-cert.sh
	python3 tests/https-g1/acceptance.py --glon ./glon
	$(MAKE) --no-print-directory glon MODULES=file,http
	python3 tests/https-g1/acceptance.py --glon ./glon
	$(MAKE) --no-print-directory glon MODULES=http

# ---- Optional HTTPS module (G2) --------------------------------------------
# Retain the G1 suite, then exercise G2 routing/inspection under both HTTP and
# file+http builds.  Bounded and local-only; requires libssl-dev and openssl.
https-g2-test:
	./check-frozen-s1.sh
	$(MAKE) --no-print-directory glon MODULES=
	@./glon tests/https-g1/minimal-smoke.glon | grep -qx minimal-ok
	$(MAKE) --no-print-directory glon MODULES=file
	$(MAKE) --no-print-directory glon MODULES=http
	tests/https-g1/make-cert.sh
	python3 tests/https-g1/acceptance.py --glon ./glon
	python3 tests/https-g2/acceptance.py --glon ./glon
	$(MAKE) --no-print-directory glon MODULES=file,http
	python3 tests/https-g1/acceptance.py --glon ./glon
	python3 tests/https-g2/acceptance.py --glon ./glon
	$(MAKE) --no-print-directory glon MODULES=http

# ---- Optional HTTPS module (G3) --------------------------------------------
# Retains G1/G2, then runs G3 under HTTP and file+http.  G3 opts into the
# documented loader-heap extension into the unused M1 task arena because the
# application plus transient request source exceeds the default loader heap.
https-g3-test:
	./check-frozen-s1.sh
	$(MAKE) --no-print-directory glon MODULES=
	@./glon tests/https-g1/minimal-smoke.glon | grep -qx minimal-ok
	$(MAKE) --no-print-directory glon MODULES=file
	$(MAKE) --no-print-directory glon MODULES=http
	tests/https-g1/make-cert.sh
	python3 tests/https-g1/acceptance.py --glon ./glon
	python3 tests/https-g2/acceptance.py --glon ./glon
	$(MAKE) --no-print-directory glon MODULES=http GLON_HTTP_RECLAIM_M1=1
	python3 tests/https-g3/acceptance.py --glon ./glon
	$(MAKE) --no-print-directory glon MODULES=file,http GLON_HTTP_RECLAIM_M1=1
	python3 tests/https-g1/acceptance.py --glon ./glon
	python3 tests/https-g2/acceptance.py --glon ./glon
	# G3 stays HTTP-only: the file module's Glon libraries consume enough of
	# the 7,232-cell managed heap that the membership app is not reliable in
	# the combined build.  G1/G2 still verify MODULES=file,http.
	$(MAKE) --no-print-directory glon MODULES=http GLON_HTTP_RECLAIM_M1=1

# ---- G3A: grouped membership register with NDJSON persistence --------------
# The app lives in examples/https-g3a and uses the large profile.  The caller
# supplies the local OpenSSL C_INCLUDE_PATH/LIBRARY_PATH (see the build notes),
# or installs libssl-dev.  The acceptance suite is local-only and bounded.
https-g3a-test:
	$(MAKE) --no-print-directory glon MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/https-g3a/acceptance.py --glon $(CURDIR)/glon
.PHONY: https-g3a-test

https-g3a-scale:
	$(MAKE) --no-print-directory glon MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/https-g3a/scale.py --glon $(CURDIR)/glon --sizes 10,100,1000
.PHONY: https-g3a-scale

https-g3a-endurance:
	$(MAKE) --no-print-directory glon MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/https-g3a/endurance.py --glon $(CURDIR)/glon --requests 1000 --workers 1
.PHONY: https-g3a-endurance

# ---- G3E: HTTPS + SQLite message application -------------------------------
# The app lives in examples/https-g3e and uses the large profile with the
# file, http and sqlite modules.  The acceptance suite is local-only, bounded
# and drives keep-alive clients (a fresh connection per request would trigger
# the documented single-thread accept/keep-alive starvation).
https-g3e-test:
	$(MAKE) --no-print-directory glon MODULES=file,http,sqlite MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/https-g3e/acceptance.py --glon $(CURDIR)/glon
.PHONY: https-g3e-test

# ---- G3F: HTTPS concurrency via a bounded CivetWeb worker pool --------------
# Same app as G3E.  --http-threads N (default 1) sizes the connection-level
# worker pool; exactly one thread is inside the Glon session at a time (the
# host's evaluator-ownership mutex), so overlapping connections make progress
# while the single interpreter and SQLite connection stay serialised.
https-g3f-test:
	$(MAKE) --no-print-directory glon MODULES=file,http,sqlite MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/https-g3f/acceptance.py --glon $(CURDIR)/glon
.PHONY: https-g3f-test

# ---- M2: loadable cooperative multitasking ----------------------------------
# Builds a task-enabled CLI (modules/task.glon + host task pump) with ten task
# slots, then runs the cooperative-task acceptance suite.  The frozen 64K
# conformance build is unaffected: it does not select MODULES=task.
m2-test:
	$(MAKE) --no-print-directory glon MODULES=file,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
	python3 tests/m2/acceptance.py --glon $(CURDIR)/glon
.PHONY: m2-test

# ---- M3: multiple outstanding asynchronous I/O operations -------------------
# Builds a file+sqlite+task CLI and runs the bounded multi-outstanding suite.
m3-test:
	$(MAKE) --no-print-directory glon MODULES=file,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
	python3 tests/m3/acceptance.py --glon $(CURDIR)/glon
.PHONY: m3-test

# ---- M4: reusable tasks and reliable lifecycle management -------------------
# Builds a file+task CLI and runs the bounded recycling/endurance suite.
m4-test:
	$(MAKE) --no-print-directory glon MODULES=file,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
	python3 tests/m4/acceptance.py --glon $(CURDIR)/glon
.PHONY: m4-test

# ---- H1: HTTPS requests as cooperative tasks (investigation) ---------------
# The CivetWeb begin_request boundary is synchronous, so this runs the
# scheduler-level interleave demonstration and documents the obstacle; see
# H1-RESULTS.md.  G1-G3F remain the supported HTTPS path.
h1-test:
	$(MAKE) --no-print-directory glon MODULES=file,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
	python3 tests/h1/acceptance.py --glon $(CURDIR)/glon
.PHONY: h1-test

# ---- H2Q: async continuation ownership tests -------------------------------
h2q-test:
	$(MAKE) --no-print-directory glon MODULES=task MEM_PROFILE=large MEM_LOADER_CELLS=500000
	python3 tests/h2q/ownership.py --glon $(CURDIR)/glon
.PHONY: h2q-test

# ---- H2A: bounded asynchronous HTTP request/response bridge ---------------
# Opt-in --http-bridge: CivetWeb workers enqueue into a bounded table and wait;
# one interpreter-owner loop runs the Glon application.  Default (non-bridge)
# G1-G3F behaviour is unchanged.
h2a-test:
	$(MAKE) --no-print-directory glon MODULES=file,http,sqlite MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/h2a/acceptance.py --glon $(CURDIR)/glon
.PHONY: h2a-test

# ---- H2B: cooperative HTTP request scheduling (partial) -------------------
# Opt-in --http-tasks spawns a Glon task per request.  The 10-task success
# criterion is blocked by the per-task return-stack vs. M1-arena limit; this
# target builds a 3-task config and runs the partial smoke only.  See
# H2B-RESULTS.md.  G3F/H2A remain the supported HTTPS paths.
h2b-test:
	$(MAKE) --no-print-directory glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256
	tests/https-g1/make-cert.sh
	python3 tests/h2b/smoke.py
.PHONY: h2b-test

# H3C: HTTP method propagation into cooperative Glon tasks (method echo,
# POST/SQLite effect, and mixed-method interleaving).  See H3C-RESULTS.md.
h3c-test:
	$(MAKE) --no-print-directory glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256
	tests/https-g1/make-cert.sh
	python3 tests/h3c/acceptance.py
.PHONY: h3c-test

# H3D: HTTP task lifetime, cancellation and loader reclamation (1,000-request
# stress, disconnects, capacity, error isolation, restart).  See H3D-RESULTS.md.
h3d-test:
	$(MAKE) --no-print-directory glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256
	tests/https-g1/make-cert.sh
	python3 tests/h3d/acceptance.py
.PHONY: h3d-test

# G3G: persistent rotating activity log for the native Glon Worlds demo.
# See G3G-RESULTS.md.
g3g-test:
	$(MAKE) --no-print-directory glon MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/glon-worlds-g3g/smoke.py
.PHONY: g3g-test

# G3H: loopback-only SQLite user registry for the native Glon Worlds demo.
# See G3H-RESULTS.md.
g3h-test:
	$(MAKE) --no-print-directory glon MODULES=file,http,sqlite MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/glon-worlds-g3h/smoke.py
.PHONY: g3h-test

# G3J: password auth is BLOCKED by the fixed 512-symbol interpreter table
# (SYM_CAP) when the G3H server + http/json/sqlite/sec/web libraries are loaded.
# This target builds with sec and documents the blocker deterministically.
# See G3J-RESULTS.md.
g3j-test:
	$(MAKE) --no-print-directory glon MODULES=file,http,sqlite,sec MEM_PROFILE=large MEM_LOADER_CELLS=500000
	tests/https-g1/make-cert.sh
	python3 tests/g3j/blocker_check.py
.PHONY: g3j-test

# Focused M1 test binary for portable distribution smoke tests.  The full
# native `s1` suite needs repository source fixtures and is not shipped in
# every platform package.
m1-smoke: tests/m1_smoke.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c s1.c r0_s1.h s1.h m1_layout.h
	$(CC) $(CFLAGS) -I. -o $@$(if $(filter Windows_NT,$(OS)),.exe,) tests/m1_smoke.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c s1.c

m1-large-test: tests/m1_large_driver.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c $(GLON_PROFILE_DEPS)
	@test "$(MEM_PROFILE)" = large || { echo "use MEM_PROFILE=large"; exit 2; }
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o m1-large-test tests/m1_large_driver.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c $(GLON_S1_SRC)
	./m1-large-test
.PHONY: m1-large-test

# ---- Desktop proof: native Glon application + browser/WASM Glon View --------
# glon-desktop is the installed application host: it binds a localhost TCP
# listener, serves the browser/WASM Glon View, opens the default browser, and
# performs the browser <-> native file round trip. The host supplies only
# primitive capabilities (TCP, filesystem read, process/open); the application
# and protocol decisions live in Glon (desktop/app.glon, desktop/view.glon).
DESKTOP_CORE = r0_s1_g1a_live.c r0_s1_g1a.c r0_s1_show.c r0_s1_runtime.c s1.c
DESKTOP_SRCS = desktop/glon_desktop.c desktop/glon_host_posix.c desktop/glon_app.c $(DESKTOP_CORE)
WINDOWS_CC ?= x86_64-w64-mingw32-gcc

# Stage the shared Glon libraries beside the page. The browser wasm is a real
# target below: the desktop uses the GLON_LIVE build (glon_event_bytes) when it
# has been built, and keeps any already-staged wasm otherwise.
.PHONY: desktop-assets
desktop-assets:
	cp jupyter/prelude.glon desktop/prelude.glon
	cp demo/shop/strings.glon desktop/strings.glon

glon-desktop: $(DESKTOP_SRCS) desktop/glon_host.h desktop/glon_app.h r0_s1.h r0_s1_g1a.h r0_s1_g1a_live.h s1.h m1_layout.h | desktop-assets
	$(CC) $(CFLAGS) -I. -o $@ $(DESKTOP_SRCS)

# The browser runtime uses the GLON_LIVE ABI (raw-byte glon_event_bytes). Build
# it with emcc, then stage it as desktop/glon.wasm.
desktop/glon.wasm: demo/shop/glon-live.wasm
	cp $< $@

desktop-wasm: desktop/glon.wasm
	@echo "staged $< (GLON_LIVE, exports glon_event_bytes)"

# Bulk-streaming fixture (~2 MB). Test data only; not part of the application.
desktop/big.txt:
	sh desktop/make-big.sh

desktop-live: glon-desktop desktop/glon.wasm desktop/big.txt
	@echo "desktop live proof ready: run ./glon-desktop desktop/app.glon"

# Native Windows executable (cross-compiled with MinGW-w64). The Windows host
# implementation links Winsock2 and shell32; see desktop/glon_host_windows.c.
glon-desktop.exe: desktop/glon_desktop.c desktop/glon_host_windows.c desktop/glon_app.c desktop/glon_host.h desktop/glon_app.h \
                  $(DESKTOP_CORE) r0_s1.h r0_s1_g1a.h r0_s1_g1a_live.h s1.h m1_layout.h | desktop-assets
	$(WINDOWS_CC) $(CFLAGS) -I. -o $@ desktop/glon_desktop.c desktop/glon_host_windows.c desktop/glon_app.c \
		$(DESKTOP_CORE) -lws2_32 -lshell32

desktop-test: desktop/desktop_tests.c desktop/glon_app.c r0_s1_g1a_live.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
              desktop/glon_app.h r0_s1.h r0_s1_g1a.h r0_s1_g1a_live.h s1.h m1_layout.h
	$(CC) $(CFLAGS) -I. -o $@ desktop/desktop_tests.c desktop/glon_app.c r0_s1_g1a_live.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./desktop-test

# HTTP-level authority regression: only Glon-authorised logical names resolve.
desktop-security-test: glon-desktop desktop/big.txt
	sh desktop/security_test.sh

# Glon app model: manifest -> modules/permissions; trusted grants; enforcement.
desktop-app-test: glon-desktop
	sh apps/glon-fetch/security_test.sh

# Real download through the app (bulk bytes bypass the Glon heap).
desktop-fetch-test: glon-desktop
	sh apps/glon-fetch/download_test.sh

# D8: per-application identity + per-app authority.
desktop-d8-test: glon-desktop
	sh apps/d8_authority_test.sh

# D9: semantic permission contracts + introspection.
desktop-d9-test: glon-desktop
	sh apps/d9_semantics_test.sh

# D10: usable Glon Fetch (success, cancel, recovery, errors).
desktop-usable-test: glon-desktop
	sh apps/glon-fetch/usable_test.sh

# D11: Glon Patrol rule tests (the game runs its own assertions in Glon).
patrol-test: demos/patrol/patrol_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
             r0_s1.h r0_s1_g1a.h s1.h m1_layout.h | glon-lib
	$(CC) $(CFLAGS) -I. -o $@ demos/patrol/patrol_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./patrol-test

# D11: stage and serve the browser/WASM Glon Patrol demo (no native host).
demos/patrol/glon.wasm: demo/shop/glon.wasm
	cp $< $@
demos/patrol/prelude.glon: jupyter/prelude.glon
	cp $< $@
demos/patrol/strings.glon: demo/shop/strings.glon
	cp $< $@

patrol-serve: demos/patrol/glon.wasm demos/patrol/prelude.glon demos/patrol/strings.glon
	sh demos/patrol/run.sh

# ---- D12A: Attack of the Mutant Kaka rule tests (game asserts in Glon) -----
# Kaka uses no M1 multitasking tasks, so its runtime reclaims the reserved M1
# task arena as loader heap (R0S1_HEAP_LIMIT override). The shared shop WASM
# keeps the default 14800-cell loader heap (Linda does use tasks).
KAKA_HEAP_LIMIT = 65536
KAKA_LIMIT_FLAG = -DR0S1_HEAP_LIMIT=$(KAKA_HEAP_LIMIT)

demo/shop/glon-kaka.wasm: standalone/glon.c r0_s1_g1a.c r0_s1_g1a.h s1.c s1.h r0_s1_runtime.c r0_s1_show.c r0_s1.h
	$(EMCC) $(STANDALONE_FLAGS) $(KAKA_LIMIT_FLAG) standalone/glon.c r0_s1_g1a.c s1.c r0_s1_runtime.c r0_s1_show.c -o $@

kaka-test: demos/kaka/kaka_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
           r0_s1.h r0_s1_g1a.h s1.h m1_layout.h kaka-render-test kaka-damage-test kaka-terminal-test kaka-wave-test kaka-fighter-test kaka-life-test | glon-lib
	$(CC) $(CFLAGS) $(KAKA_LIMIT_FLAG) -I. -o $@ demos/kaka/kaka_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./kaka-test

# D12S.12: helmet-route (fighter) tests. Loads kaka-fighter.glon WITHOUT the
# self-test module: the two together exceed the loader heap, so the fighter
# gets its own harness (the self-test rules stay covered by kaka-test).
kaka-fighter-test: demos/kaka/kaka_fighter_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
                   r0_s1.h r0_s1_g1a.h s1.h m1_layout.h | glon-lib
	$(CC) $(CFLAGS) $(KAKA_LIMIT_FLAG) -I. -o $@ demos/kaka/kaka_fighter_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./kaka-fighter-test

# D12A.7: render-binding regression -- damages each gameplay tree separately
# and proves only that tree's rendered instance changes (test-only overlay).
kaka-render-test: demos/kaka/kaka_render_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
                  r0_s1.h r0_s1_g1a.h s1.h m1_layout.h | glon-lib
	$(CC) $(CFLAGS) $(KAKA_LIMIT_FLAG) -I. -o $@ demos/kaka/kaka_render_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./kaka-render-test

# D12A: end-to-end damage-path test -- drives the REAL attack machinery
# (one attacker, real move-all dispatch) and proves only the selected tree's
# F_A changes, for each tree and each damaging actor kind, plus retargeting.
kaka-damage-test: demos/kaka/kaka_damage_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
                  r0_s1.h r0_s1_g1a.h s1.h m1_layout.h | glon-lib
	$(CC) $(CFLAGS) $(KAKA_LIMIT_FLAG) -I. -o $@ demos/kaka/kaka_damage_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./kaka-damage-test

# D12A: one authoritative terminal-state predicate; HUD and splash must agree.
kaka-terminal-test: demos/kaka/kaka_terminal_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
                    r0_s1.h r0_s1_g1a.h s1.h m1_layout.h | glon-lib
	$(CC) $(CFLAGS) $(KAKA_LIMIT_FLAG) -I. -o $@ demos/kaka/kaka_terminal_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./kaka-terminal-test

# D12B: rat waves, tree climbing/threats, and branch cover blocking berries.
kaka-wave-test: demos/kaka/kaka_wave_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
                r0_s1.h r0_s1_g1a.h s1.h m1_layout.h | glon-lib
	$(CC) $(CFLAGS) $(KAKA_LIMIT_FLAG) -I. -o $@ demos/kaka/kaka_wave_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./kaka-wave-test

# D12A: lives / laser hazard / game-over / high-score rendered-output tests.
kaka-life-test: demos/kaka/kaka_life_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
                r0_s1.h r0_s1_g1a.h s1.h m1_layout.h | glon-lib
	$(CC) $(CFLAGS) $(KAKA_LIMIT_FLAG) -I. -o $@ demos/kaka/kaka_life_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./kaka-life-test

# D12A: headless WASM verification + tick soak (the Kaka WASM + node).
wasm-kaka-test: demo/shop/glon-kaka.wasm
	node demos/kaka/kaka_wasm_test.js | tee /tmp/opencode_kaka_wasm.out
	@grep -q "KAKA_WASM_TEST PASS" /tmp/opencode_kaka_wasm.out && \
	 echo "wasm-kaka-test: PASS (kaka selftest + tick soak on real WASM)"

# D12A: stage and serve the browser/WASM Kaka demo (no native host).
demos/kaka/glon.wasm: demo/shop/glon-kaka.wasm
	cp $< $@
demos/kaka/prelude.glon: jupyter/prelude.glon
	cp $< $@
demos/kaka/strings.glon: demo/shop/strings.glon
	cp $< $@

kaka-serve: demos/kaka/glon.wasm demos/kaka/prelude.glon demos/kaka/strings.glon
	sh demos/kaka/run.sh

# D12A: real-browser regression for "first frame renders but the simulation never
# advances". Needs a debug Chrome and node (see demos/kaka/tick_regression_cdp.js);
# not part of the default suite. Override TARGET/CDP_BASE via the environment.
kaka-browser-regression:
	node demos/kaka/tick_regression_cdp.js

# D12A.2: real-browser regression that the simulation rate is independent of the
# display refresh rate (60/120/144 Hz). Needs a debug Chrome + node.
kaka-refresh-regression:
	node demos/kaka/refresh_rate_regression_cdp.js

# D12A: real-browser long-session regression for "Space eventually stopped
# firing ordinary berries" (a leaked BERRY_MAX pool). Needs a debug Chrome + node.
kaka-berry-regression:
	node demos/kaka/berry_regression_cdp.js

# D12S.13: real-browser smoke for the developer start URLs
# (?start=forest|cave|helmet). Needs a debug Chrome + node.
kaka-start-smoke:
	node demos/kaka/start_url_smoke.js

# ---- Experimental GLON_LIVE host boundary (native focused test) -------------
# Builds only the live layer + runtime; does not touch the s1 test binary or
# any WASM build. Run: make glon-live-native-test && ./glon-live-native-test
glon-live-native-test: r0_s1_g1a_live_tests.c r0_s1_g1a_live.c r0_s1_g1a_live.h r0_s1_g1a.c r0_s1_g1a.h r0_s1_runtime.c s1.c r0_s1.h s1.h m1_layout.h
	$(CC) $(CFLAGS) -DGLON_LIVE -I. -o $@ r0_s1_g1a_live_tests.c r0_s1_g1a_live.c r0_s1_g1a.c r0_s1_runtime.c s1.c

# ---- Persistent native Glon session host (Jupyter milestone, phase 1) -------
# One process = one Glon session: the runtime is initialised once and cells run
# in it one after another (see jupyter/host/glon_kernel_host.c for the framed
# request/result protocol). host-test drives it with a stdlib-only Python
# harness (no Jupyter). Linux/WSL (POSIX fds + fopencookie).
HOST_SRCS = jupyter/host/glon_kernel_host.c r0_s1_session.c r0_s1_show.c r0_s1_runtime.c s1.c
jupyter/host/glon-kernel-host: $(HOST_SRCS) r0_s1.h s1.h m1_layout.h
	$(CC) $(CFLAGS) -I. -o $@ $(HOST_SRCS)

glon-kernel-host: jupyter/host/glon-kernel-host

host-test: jupyter/host/glon-kernel-host
	$(PYTHON) jupyter/tests/test_host.py

# The Jupyter kernel over that host, through the real protocol (jupyter_client;
# no JupyterLab). Needs a Python with ipykernel, e.g.
#   make kernel-test PYTHON=~/.venvs/glon-jupyter/bin/python
PYTHON ?= python3
kernel-test: jupyter/host/glon-kernel-host
	$(PYTHON) jupyter/tests/test_kernel.py

# Install the Glon kernelspec for this user (repo-checkout install).
kernel-install: jupyter/host/glon-kernel-host
	cd jupyter && $(PYTHON) -m glon_kernel.install

# Install the kernelspec into a temporary prefix, discover it with `jupyter
# kernelspec list`, check kernel.json and run one cell through it.
kernelspec-test: jupyter/host/glon-kernel-host
	$(PYTHON) jupyter/tests/test_kernelspec.py

# ---- Traffic simulator benchmark (post-Alpha application; no language change) -
# load-once benchmark of the IDM sim. Builds both the shipped -O0 runtime and a
# directly-compiled -O2 runtime so interpreter cost can be separated from C
# optimisation level. Not part of the test suite.
traffic-bench: traffic-bench-o0 traffic-bench-o2

traffic-bench-o0: r0_s1_traffic_bench.c r0_s1_runtime.o s1.o
	$(CC) $(CFLAGS) -o $@ r0_s1_traffic_bench.c r0_s1_runtime.o s1.o

traffic-bench-o2: r0_s1_traffic_bench.c r0_s1_runtime.c s1.c
	$(CC) -std=c17 -O2 -o $@ r0_s1_traffic_bench.c r0_s1_runtime.c s1.c

.PHONY: all test clean wasm wasm-test wasm-standalone wasm-standalone-test wasm-g1a wasm-g1a-test traffic-bench traffic-bench-o0 traffic-bench-o2 wasm-traffic-test linda.html wasm-linda-test wasm-binding-test wasm-abi-test glon-live-native-test wasm-live-test primer.html wasm-primer-test glon glon-lib glon-smoke io-file-c-test io-file-test io-file-stream-test io-file-stream-scale io-file-stream-4m io-file-inplace-test io-file-inplace-bench glon-kernel-host host-test kernel-test kernel-install kernelspec-test io-file-windows-test m1-smoke glon-desktop desktop-test desktop-wasm desktop-live desktop-security-test desktop-app-test desktop-fetch-test desktop-d8-test desktop-d9-test desktop-usable-test patrol-test patrol-serve kaka-test kaka-render-test kaka-life-test kaka-damage-test kaka-terminal-test kaka-wave-test kaka-serve wasm-kaka-test kaka-browser-regression kaka-refresh-regression

# ---- machine integrity (MACHINE-INTEGRITY-AUDIT.md) ------------------------
# Machine-level invariants, separate from the functional suite (./s1): the
# boundary regressions, determinism and residue independence, recovery to a
# fixed state after every failure kind, runtime-code immutability, binary
# STRING! preservation under GC churn, and the known defects as strict
# expected failures (XFAIL; an XPASS fails the target so that a repaired
# defect's check is promoted deliberately). The masm label/reference table
# bounds are also checked in an AddressSanitizer probe, one block per
# process, so that any out-of-bounds access is caught.
INTEGRITY_SRCS = r0_s1_integrity_tests.c r0_s1_gc_interior_tests.c r0_s1_masm_bound_tests.c \
	r0_s1_runtime.c r0_s1_session.c r0_s1_show.c s1.c
ASAN_PROBE_SRCS = integrity/asan_probe.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c s1.c

machine-integrity-test: $(INTEGRITY_SRCS) r0_s1.h s1.h m1_layout.h
	$(CC) $(CFLAGS) -I. -o $@ $(INTEGRITY_SRCS)

integrity-asan-probe: $(ASAN_PROBE_SRCS) r0_s1.h s1.h m1_layout.h
	$(CC) -std=c17 -g -O1 -fsanitize=address -fno-omit-frame-pointer -I. -o $@ $(ASAN_PROBE_SRCS)

machine-integrity: machine-integrity-test integrity-asan-probe glon
	./check-frozen-s1.sh
	./machine-integrity-test
	./integrity/asan-probe.sh ./integrity-asan-probe
	./integrity/parity.sh native
	@echo "machine-integrity: PASS"

# the Glon-level integrity tests (integrity/*.glon) in the standalone WASM
# build: each file's "parity:" lines must equal integrity/expected/, as they
# must natively (needs emsdk: EMSDK=~/emsdk by default)
EMSDK ?= $(HOME)/emsdk
INTEGRITY_EMCC ?= $(EMSDK)/upstream/emscripten/emcc
INTEGRITY_NODE ?= $(firstword $(wildcard $(EMSDK)/node/*/bin/node) node)
integrity/build/glon.wasm: standalone/glon.c s1.c r0_s1_runtime.c r0_s1_show.c r0_s1_g1a.c r0_s1.h s1.h m1_layout.h
	@mkdir -p integrity/build
	EM_NODE_JS=$(INTEGRITY_NODE) $(INTEGRITY_EMCC) -O1 -nostdlib -fno-builtin \
	  -s STANDALONE_WASM=1 -s ALLOW_MEMORY_GROWTH=1 -Wl,--no-entry -Wl,--export-memory -I. \
	  standalone/glon.c s1.c r0_s1_runtime.c r0_s1_show.c r0_s1_g1a.c -o $@

machine-integrity-wasm: integrity/build/glon.wasm glon
	./integrity/parity.sh native
	./integrity/parity.sh wasm integrity/build/glon.wasm $(INTEGRITY_NODE)
	@echo "machine-integrity-wasm: PASS (native and WASM output as expected, identical)"

.PHONY: machine-integrity machine-integrity-wasm

# ---- JSON vocabulary tests (ordinary Glon; no OpenSSL) ---------------------
# The general JSON module and its full focused suite exceed the constrained
# profile's 14,800-cell loader heap, so this test build uses the documented
# large memory profile. MODULES= keeps it minimal: no optional module or
# OpenSSL is compiled or linked. strings-ext.glon is loaded explicitly (the
# minimal CLI loads only prelude + strings); loading it as a FILE also prints
# its final closure result, so that one #[func] line is filtered out.
json-test:
	$(MAKE) --no-print-directory glon MODULES= MEM_PROFILE=large
	@out=$$(./glon glon-lib/strings-ext.glon modules/json.glon tests/json/tests.glon); status=$$?; \
	printf '%s\n' "$$out" | grep -v '^#\[func\]$$'; exit $$status
.PHONY: json-test

# ---- Cross-platform CI probes (host portability, no language change) -------
# arch-check prints the cell width, alignment, pointer width and the exact
# boundary constants used by the frozen 64K map; the CI matrix requires a
# 64-bit cell and checks the same invariant on every runner.
arch-check: ci/arch_check.c s1.c r0_s1.h s1.h m1_layout.h
	$(CC) $(CFLAGS) -I. -o $@ ci/arch_check.c s1.c

# Iterator vocabulary tests live in experiments/glon-iter; they build a fresh
# CLI from the unchanged runtime and run the native iterator/lexer suites.
iterators-test:
	$(MAKE) -C experiments/glon-iter test
.PHONY: arch-check iterators-test
