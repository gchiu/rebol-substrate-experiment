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

test: s1 gc-worklist-overflow-test global-ctx-hash-constrained
	./check-frozen-s1.sh
	./s1
	./gc-worklist-overflow-test
	./global-ctx-hash-constrained
	python3 demo/shop/build-primer.py --check
	$(MAKE) --no-print-directory test-vocab1

# GLON-VOCAB-1 (glon-gui-f2 3b7b7f7e; GLON-VIEW-CORE-V1 step 5): vocabularies
# and lexical binding through the native runner (the minimal CLI).
test-vocab1:
	$(MAKE) --no-print-directory glon MODULES=
	sh tests/vocab1/run.sh ./glon
.PHONY: test-vocab1

# GLON-CORE-C1: global-context hash regression (see global_ctx_hash_test.c).
# global-ctx-hash-constrained is the 64K reference build (256 globals, 512
# symbols, radix 256, the original encoding checked cell by cell).
# global-ctx-hash-test also builds every declared profile (C2: capacities
# 256 ... 16,384) and runs the same checks there.  hash-capacity-check
# confirms that unsafe capacity combinations are rejected at compile time.
global-ctx-hash-constrained: global_ctx_hash_test.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c s1.c r0_s1.h s1.h m1_layout.h
	$(CC) $(CFLAGS) -I. -o $@ global_ctx_hash_test.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c s1.c

global-ctx-hash-test: global-ctx-hash-constrained
	./global-ctx-hash-constrained
	@for p in $(filter-out constrained,$(MEM_PROFILES)); do \
	  $(MAKE) --no-print-directory global-ctx-hash-profile MEM_PROFILE=$$p || exit 1; done

# Each unsafe combination must fail to compile with its own assertion; the
# last, safe one must compile.
hash-capacity-check:
	@check() { want="$$1"; shift; \
	  if $(CC) -std=c17 -fsyntax-only -I. "$$@" r0_s1_runtime.c 2>$@.err; then \
	    echo "  FAIL: $$* compiled (expected: $$want)"; exit 1; \
	  elif grep -q "$$want" $@.err; then echo "  ok: $$* rejected ($$want)"; \
	  else echo "  FAIL: $$* rejected for another reason:"; cat $@.err; exit 1; fi; }; \
	check "at least the 64K reference" -DR0S1_GLOBAL_CAP=128 -DR0S1_SYM_CAP=512; \
	check "its own symbol" -DR0S1_GLOBAL_CAP=512 -DR0S1_SYM_CAP=256; \
	check "overflow a 32-bit cell" -DR0S1_GLOBAL_CAP=4096 -DR0S1_SYM_CAP=1048576; \
	check "does not fit in the loader heap" -DR0S1_GLOBAL_CAP=8192 -DR0S1_SYM_CAP=16384; \
	$(CC) -std=c17 -fsyntax-only -I. -DR0S1_GLOBAL_CAP=4096 -DR0S1_SYM_CAP=8192 r0_s1_runtime.c && \
	  echo "  ok: 4096 / 8192 compiles"; rm -f $@.err; echo "hash-capacity-check: PASS"
.PHONY: global-ctx-hash-test global-ctx-hash-profile hash-capacity-check

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
	rm -f .glon-config .glon-config.tmp io-file-c-test arch-check m1-smoke m1-smoke.exe

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

# GLON-VIEW-CORE-V5: standalone/glon.c calls r0_s1_g1a_event /
# r0_s1_g1a_event_value outside its GLON_LIVE branch, so r0_s1_g1a.c is
# linked here as in demo/shop/glon.wasm (without it a fresh build failed to
# link and an unbuilt tree tested a stale committed module).
standalone/glon.wasm: standalone/glon.c r0_s1_g1a.c r0_s1_g1a.h s1.c s1.h r0_s1_runtime.c r0_s1_show.c r0_s1.h
	$(EMCC) $(STANDALONE_FLAGS) standalone/glon.c r0_s1_g1a.c s1.c r0_s1_runtime.c r0_s1_show.c \
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
# ---- Memory profiles (GLON-CORE-C2) -----------------------------------------
# One mechanism: every profile is declared, all capacities together, in
# profiles/<name>.profile, and scripts/glon-profile.sh generates
# build/profile/<name>/ from it:
#   glon_profile.h   every layout/capacity constant (force-included, -include)
#   s1_profile.[ch]  the frozen S1 with only MEM_CELLS (and DS_INIT, when the
#                    profile moves the code / data-stack boundary) replaced
#   layout.txt       the region map
# r0_s1_layout.h (included by the runtime) rejects any overlapping or unsafe
# layout at compile time, however its constants were supplied.
#
#   constrained   the 64K reference: frozen s1.c and the header defaults,
#                 nothing generated (the default and conformance profile)
#   other names   profiles/*.profile; `make profile-list`
#
# Capacities are never overridden one at a time: MEM_MANAGED_CELLS,
# MEM_LOADER_CELLS, MEM_GLOBAL_CAP, ... are rejected; add or edit a profile.
MEM_PROFILE ?= constrained
MEM_PROFILES := $(sort $(basename $(notdir $(wildcard profiles/*.profile))))
MEM_LEGACY_OVERRIDES := MEM_MANAGED_CELLS MEM_WORKLIST_CELLS MEM_LOADER_CELLS \
    MEM_M1_CELLS MEM_TOTAL MEM_GLOBAL_CAP MEM_SYM_CAP MEM_CODE_CAP MEM_GLOBAL_CONTEXT_CAP
$(foreach v,$(MEM_LEGACY_OVERRIDES),$(if $(filter command line environment,$(origin $(v))),$(error $(v) is no longer a build option: capacities are declared together in profiles/*.profile (MEM_PROFILE = $(MEM_PROFILES)))))
# Deprecated spelling of the 64K no-task profile (D1: -DR0S1_HEAP_LIMIT=65000).
GLON_HTTP_RECLAIM_M1 ?= 0
ifeq ($(GLON_HTTP_RECLAIM_M1),1)
  ifeq ($(origin MEM_PROFILE),command line)
    $(error GLON_HTTP_RECLAIM_M1=1 and MEM_PROFILE are exclusive; use MEM_PROFILE=constrained-notask)
  endif
  override MEM_PROFILE := constrained-notask
endif
GLON_MEMORY_CFLAGS :=
GLON_PROFILE_DEPS :=
GLON_S1_SRC := s1.c
ifneq ($(MEM_PROFILE),constrained)
  ifeq ($(filter $(MEM_PROFILE),$(MEM_PROFILES)),)
    $(error unknown MEM_PROFILE='$(MEM_PROFILE)'; supported values: $(MEM_PROFILES))
  endif
  MEM_PROFILE_DIR := build/profile/$(MEM_PROFILE)
  GLON_MEMORY_CFLAGS := -include $(MEM_PROFILE_DIR)/glon_profile.h
  GLON_S1_SRC := $(MEM_PROFILE_DIR)/s1_profile.c
  GLON_PROFILE_DEPS := $(MEM_PROFILE_DIR)/glon_profile.h $(MEM_PROFILE_DIR)/s1_profile.c $(MEM_PROFILE_DIR)/s1_profile.h
endif

# One generation produces all three files of a profile directory; it reruns
# only when the profile file, the generator or the frozen S1 sources change.
build/profile/%/glon_profile.h build/profile/%/s1_profile.c build/profile/%/s1_profile.h: profiles/%.profile scripts/glon-profile.sh s1.c s1.h
	sh scripts/glon-profile.sh $< build/profile/$*

profile-list:
	@for p in $(MEM_PROFILES); do printf '%-20s %s\n' "$$p" "$$(grep -m1 '^STATUS=' profiles/$$p.profile | cut -d= -f2)"; done
.PHONY: profile-list

# Headers every build of the runtime depends on (C2: r0_s1_layout.h is new;
# m1_layout.h was missing from several rules).  Every target that compiles
# r0_s1_runtime.c gets them here, so a layout or header change cannot leave a
# stale runtime object or binary behind.
R0S1_RUNTIME_HEADERS := r0_s1.h r0_s1_layout.h m1_layout.h s1.h
R0S1_RUNTIME_TARGETS := r0_s1_runtime.o r0_s1_runtime_prof.o global-ctx-hash-constrained \
    gc-worklist-overflow-test fib-p6b-bench fib-p10a-bench fib-p10b-bench web/demo.js \
    standalone/glon.wasm demo/shop/glon.wasm demo/shop/glon-live.wasm m1-smoke desktop-test \
    patrol-test demo/shop/glon-kaka.wasm kaka-test kaka-fighter-test kaka-render-test \
    kaka-damage-test kaka-terminal-test kaka-wave-test kaka-life-test glon-live-native-test \
    traffic-bench-o2 integrity/build/glon.wasm glon m1-large-test global-ctx-hash-profile
$(R0S1_RUNTIME_TARGETS): $(R0S1_RUNTIME_HEADERS)

# Kaka (game; native tests and WASM) runs no M1 tasks and needs 25,536
# loader cells (kaka-fighter reaches 65,286): MEM_PROFILE constrained-notask.
# Its harnesses compile the frozen s1.c directly (the profile keeps the 64K
# S1 map) with the generated profile header force-included.
KAKA_PROFILE_HEADER := build/profile/constrained-notask/glon_profile.h
KAKA_TARGETS := demo/shop/glon-kaka.wasm kaka-test kaka-fighter-test kaka-render-test \
    kaka-damage-test kaka-terminal-test kaka-wave-test kaka-life-test
$(KAKA_TARGETS): $(KAKA_PROFILE_HEADER)

GLON_SRCS = glon.c r0_s1_session.c r0_s1_show.c r0_s1_runtime.c $(GLON_S1_SRC)
GLON_LIB_DIR = glon-lib
GLON_LIBS = $(GLON_LIB_DIR)/prelude.glon $(GLON_LIB_DIR)/binding.glon $(GLON_LIB_DIR)/strings.glon $(GLON_LIB_DIR)/strings-ext.glon

# Optional modules are compile-time selected. MODULES is empty for the
# unchanged minimal CLI.  The list is comma-separated:
#
#   MODULES=file        host-mediated async file module
#   MODULES=http        optional CivetWeb HTTPS listener + ordinary http.glon
#   MODULES=file,http   both optional modules
#
# Unknown names fail at build time.  Modules are additive: a minimal build
# never compiles or links any module object, source or Glon library.
comma := ,
GLON_MODULE_LIST := $(subst $(comma), ,$(strip $(MODULES)))
GLON_MODULE_UNKNOWN := $(filter-out file http,$(GLON_MODULE_LIST))
ifneq ($(strip $(GLON_MODULE_UNKNOWN)),)
  $(error unknown MODULES='$(MODULES)'; supported values: file, http, file,http)
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
# the optional HTTP module.  A normal Linux install provides them with
# `libssl-dev`; OPENSSL_CFLAGS/OPENSSL_LIBS let a local or cross environment
# point at an unpacked development package without editing the Makefile.
OPENSSL_CFLAGS ?=
OPENSSL_LIBS ?= -lssl -lcrypto

# The HTTP server does not create M1 tasks.  The G3 application plus the
# transient per-request source expression outgrow the default 14,800-cell
# loader heap; those builds select MEM_PROFILE=constrained-notask (no task
# arena or table, loader 25,536 cells).  GLON_HTTP_RECLAIM_M1=1 is a
# deprecated alias for it (see the memory-profile section).

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
CIVETWEB_FLAGS = -std=c17 -O2 -w -pthread -DOPENSSL_API_3_0 -DNO_CGI -DNO_CACHING \
		-DNO_SSL_DL -Ithird_party/civetweb $(OPENSSL_CFLAGS)
# (defined before the rule that lists it: prerequisites expand when read)
CIVETWEB_STAMP = build/civetweb.flags
third_party/civetweb/civetweb.o: third_party/civetweb/civetweb.c third_party/civetweb/civetweb.h \
		$(wildcard third_party/civetweb/*.inl) $(CIVETWEB_STAMP)
	$(CC) $(CIVETWEB_FLAGS) -c $< -o $@

# ---- Build-configuration stamps (GLON-CORE-C2) -------------------------------
# .glon-config records every value that changes the glon binary (modules,
# memory profile, compiler, flags, OpenSSL/SQLite settings) and
# build/civetweb.flags records CivetWeb's compile command.  Each is rewritten
# only when its contents change (scripts/glon-stamp.sh), so a changed
# configuration rebuilds exactly what it affects and an unchanged one rebuilds
# nothing.  They are refreshed only when a goal that links glon is built
# (GLON_LINK_GOALS; recursive `$(MAKE) glon` recipes count), so building
# other targets cannot disturb them.  `glon` is a real file target (C2: it was
# listed as .PHONY, which relinked it on every invocation).  Profile contents are tracked by real
# file dependencies (build/profile/<name>/ on profiles/<name>.profile).
GLON_CONFIG = .glon-config
GLON_LINK_GOALS := glon machine-integrity
GLON_CONFIG_LINE := modules=$(strip $(MODULES)) profile=$(MEM_PROFILE) cc=$(CC) \
    cflags=$(CFLAGS) module-cflags=$(GLON_MODULE_CFLAGS) ldlibs=$(GLON_MODULE_LDLIBS)
ifneq ($(filter $(GLON_LINK_GOALS) third_party/civetweb/civetweb.o,$(MAKECMDGOALS)),)
  $(shell sh scripts/glon-stamp.sh $(GLON_CONFIG) '$(GLON_CONFIG_LINE)')
  $(shell sh scripts/glon-stamp.sh $(CIVETWEB_STAMP) '$(CC) $(CIVETWEB_FLAGS)')
endif
$(GLON_CONFIG):
	@sh scripts/glon-stamp.sh $@ '$(GLON_CONFIG_LINE)'
$(CIVETWEB_STAMP):
	@sh scripts/glon-stamp.sh $@ '$(CC) $(CIVETWEB_FLAGS)'

# Headers of the CLI host and its modules (glon.c and module sources).
GLON_HOST_HEADERS := $(wildcard glon_*.h) r0_s1_g1a.h
ifneq ($(filter http,$(GLON_MODULE_LIST)),)
  GLON_HOST_HEADERS += third_party/civetweb/civetweb.h
endif

glon: $(GLON_SRCS) $(GLON_MODULE_SRCS) $(GLON_MODULE_OBJS) $(GLON_PROFILE_DEPS) $(R0S1_RUNTIME_HEADERS) $(GLON_HOST_HEADERS) $(GLON_CONFIG) $(GLON_TARGET_LIBS)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) $(GLON_MODULE_CFLAGS) -I. -o $@ $(GLON_SRCS) $(GLON_MODULE_SRCS) $(GLON_MODULE_OBJS) $(GLON_MODULE_LDLIBS)

$(GLON_LIB_DIR)/prelude.glon: jupyter/prelude.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/strings.glon: demo/shop/strings.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

# GLON-VOCAB-1: set for a computed global name (loaded after the prelude)
$(GLON_LIB_DIR)/binding.glon: demo/shop/binding.glon
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

glon-lib: $(GLON_LIBS)

glon-smoke: glon
	./glon-smoke.sh

# Focused module tests: no R0/S1 runtime, so sanitizer builds are practical.
io-file-c-test: glon_io_file_tests.c glon_io_file.c glon_io_file_posix.c glon_io_file_cli.c glon_io_file.h glon_io_file_cli.h
	$(CC) $(CFLAGS) -I. -o $@ glon_io_file_tests.c glon_io_file.c glon_io_file_posix.c glon_io_file_cli.c -pthread
	./io-file-c-test

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
	$(MAKE) --no-print-directory glon MODULES=http MEM_PROFILE=constrained-notask
	python3 tests/https-g3/acceptance.py --glon ./glon
	$(MAKE) --no-print-directory glon MODULES=file,http MEM_PROFILE=constrained-notask
	python3 tests/https-g1/acceptance.py --glon ./glon
	python3 tests/https-g2/acceptance.py --glon ./glon
	# G3 stays HTTP-only: the file module's Glon libraries consume enough of
	# the 7,232-cell managed heap that the membership app is not reliable in
	# the combined build.  G1/G2 still verify MODULES=file,http.
	$(MAKE) --no-print-directory glon MODULES=http MEM_PROFILE=constrained-notask

# ---- G3A: grouped membership register with NDJSON persistence --------------
# The app lives in examples/https-g3a and uses the large profile.  The caller
# supplies the local OpenSSL C_INCLUDE_PATH/LIBRARY_PATH (see the build notes),
# or installs libssl-dev.  The acceptance suite is local-only and bounded.
https-g3a-test:
	$(MAKE) --no-print-directory glon MODULES=file,http MEM_PROFILE=server
	tests/https-g1/make-cert.sh
	python3 tests/https-g3a/acceptance.py --glon $(CURDIR)/glon
.PHONY: https-g3a-test

https-g3a-scale:
	$(MAKE) --no-print-directory glon MODULES=file,http MEM_PROFILE=server
	tests/https-g1/make-cert.sh
	python3 tests/https-g3a/scale.py --glon $(CURDIR)/glon --sizes 10,100,1000
.PHONY: https-g3a-scale

https-g3a-endurance:
	$(MAKE) --no-print-directory glon MODULES=file,http MEM_PROFILE=server
	tests/https-g1/make-cert.sh
	python3 tests/https-g3a/endurance.py --glon $(CURDIR)/glon --requests 1000 --workers 1
.PHONY: https-g3a-endurance

# Focused M1 test binary for portable distribution smoke tests.  The full
# native `s1` suite needs repository source fixtures and is not shipped in
# every platform package.
m1-smoke: tests/m1_smoke.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c s1.c r0_s1.h s1.h m1_layout.h
	$(CC) $(CFLAGS) -I. -o $@$(if $(filter Windows_NT,$(OS)),.exe,) tests/m1_smoke.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c s1.c

m1-large-test: tests/m1_large_driver.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c $(GLON_PROFILE_DEPS)
	@test "$(MEM_PROFILE)" != constrained || { echo "use a non-constrained MEM_PROFILE ($(MEM_PROFILES))"; exit 2; }
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o m1-large-test tests/m1_large_driver.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c $(GLON_S1_SRC)
	./m1-large-test
.PHONY: m1-large-test

# GLON-CORE-C1: the large-profile build of the global-context hash regression
# (driven by global-ctx-hash-test; defined after the profile block so that
# GLON_PROFILE_DEPS is set when this rule is read).
global-ctx-hash-profile: global_ctx_hash_test.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c r0_s1.h s1.h m1_layout.h $(GLON_PROFILE_DEPS)
	@test "$(MEM_PROFILE)" != constrained || { echo "use a non-constrained MEM_PROFILE ($(MEM_PROFILES))"; exit 2; }
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o $@-$(MEM_PROFILE) global_ctx_hash_test.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(GLON_S1_SRC)
	./$@-$(MEM_PROFILE)


# ---- GLON-CORE-C2 profile checks ---------------------------------------------
# profile-check        generate and build every profile; the layout probe
#                      checks that its S1 copy and header agree and that a
#                      program runs; the constrained profile file must
#                      reproduce the built-in defaults exactly.
# profile-reject-check invalid or overlapping layouts are rejected.
# profile-common-test  one program, identical output on every profile.
# build-deps-check     stale-artifact regression (profiles, generator, layout
#                      headers, CivetWeb flags, profile switching).
profile-layout-one: tests/core-c2/profile_layout.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(R0S1_RUNTIME_HEADERS) $(GLON_PROFILE_DEPS)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o build/profile-layout-$(MEM_PROFILE) tests/core-c2/profile_layout.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(GLON_S1_SRC)
	./build/profile-layout-$(MEM_PROFILE)

profile-check: build/profile/constrained/glon_profile.h
	@mkdir -p build
	@$(MAKE) --no-print-directory profile-layout-one MEM_PROFILE=constrained | tee build/profile-layout.default
	$(CC) $(CFLAGS) -include build/profile/constrained/glon_profile.h -I. -o build/profile-layout-constrained-generated tests/core-c2/profile_layout.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c s1.c
	./build/profile-layout-constrained-generated > build/profile-layout.generated
	@grep '^LAYOUT ' build/profile-layout.default > build/profile-layout.default.line
	@grep '^LAYOUT ' build/profile-layout.generated > build/profile-layout.generated.line
	@cmp build/profile-layout.default.line build/profile-layout.generated.line \
	  && echo "  ok: profiles/constrained.profile reproduces the built-in 64K defaults exactly" \
	  || { echo "  FAIL: profiles/constrained.profile differs from the defaults"; exit 1; }
	@for p in $(filter-out constrained,$(MEM_PROFILES)); do \
	  $(MAKE) --no-print-directory profile-layout-one MEM_PROFILE=$$p || exit 1; done
	@echo "profile-check: PASS ($(words $(MEM_PROFILES)) profiles)"

profile-reject-check:
	sh tests/core-c2/reject_check.sh

# GLON-VIEW-CORE-V1 (SITE-1 on C2): function-site capacity in every declared
# profile -- 640 in the fixed 64K slot; 2,048 / 4,096 / 8,192 in the desktop
# profiles' site region (profiles/*.profile SITES / SITE_CAP).
site-capacity-one: tests/core-v1/site_capacity_test.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(R0S1_RUNTIME_HEADERS) $(GLON_PROFILE_DEPS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o build/site-capacity-$(MEM_PROFILE) tests/core-v1/site_capacity_test.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(GLON_S1_SRC)
	./build/site-capacity-$(MEM_PROFILE)

site-capacity-test:
	@for p in $(MEM_PROFILES); do \
	  $(MAKE) --no-print-directory site-capacity-one MEM_PROFILE=$$p || exit 1; done
	@echo "site-capacity-test: PASS ($(words $(MEM_PROFILES)) profiles)"
.PHONY: site-capacity-one site-capacity-test

# GLON-VIEW-CORE-V1: the literal-block GC hazard (known, unresolved defect) as
# an explicit regression test in the 64K, 1M and 4M profiles: the defect must
# reproduce exactly; a change is reported (and fails) so the record is updated.
LITERAL_GC_PROFILES ?= constrained desktop-1m desktop-4m
literal-block-gc-check:
	sh tests/core-v1/literal_block_gc.sh $(LITERAL_GC_PROFILES)
.PHONY: literal-block-gc-check

# GLON-VIEW-CORE-V1: differential checks of the runtime optimisations ported
# from glon-gui-f2 (GLON-MEM-2/3 allocator hint, GLON-MEM-5 closure check,
# INTENT-7 hashed set-word search, -DR0S1_SET_LINEAR), each against its switch that restores the
# old code, in the 64K, 256K, 1M and 4M profiles (tests/core-v1/mem_diff.sh).
MEM_DIFF_PROFILES ?= constrained desktop-256k desktop-1m desktop-4m
MEM_DIFF_DIR = build/core-v1
MEM_DIFF_RT = r0_s1_runtime.c r0_s1_session.c r0_s1_show.c
mem-diff-one: tests/core-v1/mem2_alloc_diff.c tests/core-v1/mem5_closure_diff.c tests/core-v1/gc_interior_main.c r0_s1_gc_interior_tests.c $(MEM_DIFF_RT) $(R0S1_RUNTIME_HEADERS) $(GLON_PROFILE_DEPS)
	@mkdir -p $(MEM_DIFF_DIR)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o $(MEM_DIFF_DIR)/mem2-$(MEM_PROFILE) tests/core-v1/mem2_alloc_diff.c $(MEM_DIFF_RT) $(GLON_S1_SRC)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -DR0S1_ALLOC_SCAN_FROM_BASE -I. -o $(MEM_DIFF_DIR)/mem2-$(MEM_PROFILE)-base tests/core-v1/mem2_alloc_diff.c $(MEM_DIFF_RT) $(GLON_S1_SRC)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o $(MEM_DIFF_DIR)/mem5-$(MEM_PROFILE) tests/core-v1/mem5_closure_diff.c $(MEM_DIFF_RT) $(GLON_S1_SRC)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -I. -o $(MEM_DIFF_DIR)/mem5-$(MEM_PROFILE)-walk tests/core-v1/mem5_closure_diff.c $(MEM_DIFF_RT) $(GLON_S1_SRC)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o $(MEM_DIFF_DIR)/interior-$(MEM_PROFILE) tests/core-v1/gc_interior_main.c r0_s1_gc_interior_tests.c $(MEM_DIFF_RT) $(GLON_S1_SRC)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -I. -o $(MEM_DIFF_DIR)/interior-$(MEM_PROFILE)-walk tests/core-v1/gc_interior_main.c r0_s1_gc_interior_tests.c $(MEM_DIFF_RT) $(GLON_S1_SRC)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -DR0S1_SET_LINEAR -I. -o $(MEM_DIFF_DIR)/mem2-$(MEM_PROFILE)-setlin tests/core-v1/mem2_alloc_diff.c $(MEM_DIFF_RT) $(GLON_S1_SRC)
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -DR0S1_SET_LINEAR -I. -o $(MEM_DIFF_DIR)/mem5-$(MEM_PROFILE)-setlin tests/core-v1/mem5_closure_diff.c $(MEM_DIFF_RT) $(GLON_S1_SRC)

mem-diff-test:
	@for p in $(MEM_DIFF_PROFILES); do \
	  $(MAKE) --no-print-directory mem-diff-one MEM_PROFILE=$$p >/dev/null || exit 1; done
	sh tests/core-v1/mem_diff.sh $(MEM_DIFF_DIR) $(MEM_DIFF_PROFILES)
.PHONY: mem-diff-one mem-diff-test

# GLON-VIEW-CORE-V1: INTENT-7's hashed set-word search on C2's fixed-radix
# hash encoding, against the linear search, in the 64K, 1M and 4M profiles.
INTENT7_PROFILES ?= constrained desktop-1m desktop-4m
intent7-set-check:
	sh tests/core-v1/intent7_set.sh $(INTENT7_PROFILES)
.PHONY: intent7-set-check

# GLON-VIEW-CORE-V2: library-load runner (a bare session, no default
# libraries), built per memory profile: build/core-v2/libload-<profile>.
libload-one: tests/core-v2/libload.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(R0S1_RUNTIME_HEADERS) $(GLON_PROFILE_DEPS)
	@mkdir -p build/core-v2
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o build/core-v2/libload-$(MEM_PROFILE) tests/core-v2/libload.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(GLON_S1_SRC)
.PHONY: libload-one

# GLON-VIEW-CORE-V2: s/index-of (C2's two-argument search, formerly s/find)
# and the shared s/find s i p, separately and together.
s-find-check:
	sh tests/core-v2/s_find_check.sh
.PHONY: s-find-check

# GLON-VIEW-CORE-V2: C2's modules as legitimate vocabulary members / loads.
modules-check:
	sh tests/core-v2/modules_check.sh
.PHONY: modules-check

# GLON-VIEW-CORE-V2: static audit of lexical-binding divergence and duplicate
# definitions over the C2 module / shared library sessions.
library-audit:
	sh tests/core-v2/library_audit.sh
.PHONY: library-audit

# GLON-VIEW-CORE-V2: the shared block-set! refuses a managed value stored into
# a literal (loader) block, in the 64K, 1M and 4M profiles.
literal-store-check:
	sh tests/core-v2/literal_store.sh $(LITERAL_GC_PROFILES)
.PHONY: literal-store-check

# GLON-VIEW-CORE-V2: the shared collection and text libraries (list.glon,
# strings-text.glon, with block.glon) in a plain session, per profile
# (tests/core-v2/lib1_tests.c, ported from glon-gui-f2 view/tests/lib1_tests.c).
LIB1_PROFILES ?= constrained desktop-1m desktop-4m
lib1-one: tests/core-v2/lib1_tests.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(R0S1_RUNTIME_HEADERS) $(GLON_PROFILE_DEPS)
	@mkdir -p build/core-v2
	$(CC) $(CFLAGS) $(GLON_MEMORY_CFLAGS) -I. -o build/core-v2/lib1-$(MEM_PROFILE) tests/core-v2/lib1_tests.c r0_s1_runtime.c r0_s1_session.c r0_s1_show.c $(GLON_S1_SRC) -lm
lib1-test:
	@set -e; for p in $(LIB1_PROFILES); do $(MAKE) --no-print-directory lib1-one MEM_PROFILE=$$p >/dev/null; rc=0; \
	  timeout 900 ./build/core-v2/lib1-$$p . > build/core-v2/lib1-$$p.txt 2>&1 || rc=$$?; \
	  grep -v '^\[dump\]' build/core-v2/lib1-$$p.txt | tail -n 3; echo "lib1 $$p: exit $$rc"; [ $$rc -eq 0 ]; done
.PHONY: lib1-one lib1-test

# GLON-VIEW-CORE-V3 capacity: code region, main return stack and recursion
# limit per profile (desktop profiles: CODE_CAP 17,408).
code-capacity-check:
	sh tests/core-v2/code_capacity.sh
.PHONY: code-capacity-check

profile-common-test:
	sh tests/core-c2/common_check.sh

build-deps-check:
	sh tests/core-c2/build_deps_check.sh
.PHONY: profile-layout-one profile-check profile-reject-check profile-common-test build-deps-check

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
# Kaka uses no M1 multitasking tasks, so it is built with the declared
# constrained-notask profile (no task arena or table; loader [40000, 65536)),
# replacing the D1 override -DR0S1_HEAP_LIMIT=65536, which let the loader
# share the task table with the task records.  The shared shop WASM keeps the
# default 14800-cell loader heap (Linda does use tasks).
KAKA_LIMIT_FLAG = -include $(KAKA_PROFILE_HEADER)

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

.PHONY: all test clean wasm wasm-test wasm-standalone wasm-standalone-test wasm-g1a wasm-g1a-test traffic-bench traffic-bench-o0 traffic-bench-o2 wasm-traffic-test linda.html wasm-linda-test wasm-binding-test wasm-abi-test glon-live-native-test wasm-live-test primer.html wasm-primer-test glon-lib glon-smoke io-file-c-test io-file-test io-file-stream-test io-file-stream-scale io-file-stream-4m io-file-inplace-test io-file-inplace-bench glon-kernel-host host-test kernel-test kernel-install kernelspec-test io-file-windows-test m1-smoke glon-desktop desktop-test desktop-wasm desktop-live desktop-security-test desktop-app-test desktop-fetch-test desktop-d8-test desktop-d9-test desktop-usable-test patrol-test patrol-serve kaka-test kaka-render-test kaka-life-test kaka-damage-test kaka-terminal-test kaka-wave-test kaka-serve wasm-kaka-test kaka-browser-regression kaka-refresh-regression

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

# Glon/View (view/; GLON-VIEW-CORE-V3): the desktop GUI built on this core
.PHONY: view-test view-demo view-probe
view-test:
	$(MAKE) -C view test

view-demo:
	$(MAKE) -C view demo

view-probe:
	$(MAKE) -C view probe
