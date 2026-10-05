CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g

OBJS = s1.o tests.o adversarial.o claims.o r0.o r0_tests.o r0_s1_runtime.o r0_s1_g1a.o r0_s1_tests.o r0_s1_debug_tests.o r0_s1_m1_tests.o r0_s1_m2_tests.o r0_s1_m3_tests.o r0_s1_m3b_tests.o r0_s1_m3c_tests.o r0_s1_m3d_tests.o r0_s1_nested_closure_tests.o r0_s1_p4_tests.o r0_s1_p5_tests.o r0_s1_g1a_tests.o r0_s1_g1b_tests.o r0_s1_g1c_tests.o r0_s1_g1d_tests.o r0_s1_g1e_tests.o r0_s1_lambda_tests.o r0_s1_masm_tests.o r0_s1_gc_safepoint_tests.o r0_s1_invoke_tests.o r0_s1_reduce_tests.o r0_s1_parse_tests.o r0_s1_equality_tests.o r0_s1_bound_tests.o r0_s1_case_tests.o r0_s1_escape_law_tests.o r0_s1_error_tests.o r0_s1_show.o r0_s1_primer_tests.o r0_s1_parse_hardening_tests.o r0_s1_context_capacity_tests.o r0_s1_session.o r0_s1_session_tests.o r0_s1_string_ops_tests.o r0_s1_traffic_tests.o r0_s1_newell_tests.o r0_s1_ovm_tests.o r0_s1_nasch_tests.o main.o

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
r0_s1_session.o: r0_s1_session.c r0_s1.h s1.h
r0_s1_session_tests.o: r0_s1_session_tests.c r0_s1.h m1_layout.h s1.h
r0_s1_string_ops_tests.o: r0_s1_string_ops_tests.c r0_s1.h m1_layout.h s1.h
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
	python3 demo/shop/build-primer.py --check

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
	rm -f glon-desktop glon-desktop.exe desktop-test patrol-test kaka-test desktop/glon.wasm desktop/prelude.glon desktop/strings.glon desktop/big.txt
	rm -rf glon glon-lib

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
GLON_SRCS = glon.c r0_s1_session.c r0_s1_show.c r0_s1_runtime.c s1.c
GLON_LIB_DIR = glon-lib
GLON_LIBS = $(GLON_LIB_DIR)/prelude.glon $(GLON_LIB_DIR)/strings.glon

glon: $(GLON_SRCS) r0_s1.h s1.h m1_layout.h $(GLON_LIBS)
	$(CC) $(CFLAGS) -I. -o $@ $(GLON_SRCS)

$(GLON_LIB_DIR)/prelude.glon: jupyter/prelude.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

$(GLON_LIB_DIR)/strings.glon: demo/shop/strings.glon
	@mkdir -p $(GLON_LIB_DIR)
	cp $< $@

glon-lib: $(GLON_LIBS)

glon-smoke: glon
	./glon-smoke.sh

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
kaka-test: demos/kaka/kaka_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c \
           r0_s1.h r0_s1_g1a.h s1.h m1_layout.h | glon-lib
	$(CC) $(CFLAGS) -I. -o $@ demos/kaka/kaka_tests.c r0_s1_g1a.c r0_s1_runtime.c s1.c
	./kaka-test

# D12A: headless WASM verification + tick soak (the real shop WASM + node).
wasm-kaka-test:
	node demos/kaka/kaka_wasm_test.js | tee /tmp/opencode_kaka_wasm.out
	@grep -q "KAKA_WASM_TEST PASS" /tmp/opencode_kaka_wasm.out && \
	 echo "wasm-kaka-test: PASS (kaka selftest + tick soak on real WASM)"

# D12A: stage and serve the browser/WASM Kaka demo (no native host).
demos/kaka/glon.wasm: demo/shop/glon.wasm
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

.PHONY: all test clean wasm wasm-test wasm-standalone wasm-standalone-test wasm-g1a wasm-g1a-test traffic-bench traffic-bench-o0 traffic-bench-o2 wasm-traffic-test linda.html wasm-linda-test wasm-binding-test wasm-abi-test glon-live-native-test wasm-live-test primer.html wasm-primer-test glon-lib glon-smoke glon-kernel-host host-test kernel-test kernel-install kernelspec-test glon-desktop desktop-test desktop-wasm desktop-live desktop-security-test desktop-app-test desktop-fetch-test desktop-d8-test desktop-d9-test desktop-usable-test patrol-test patrol-serve kaka-test kaka-serve wasm-kaka-test kaka-browser-regression kaka-refresh-regression
