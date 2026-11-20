# GLON HTTPS Server — G3A: Scalable Memory, JSON and Persistent Applications

**Status:** implemented and locally committed, not pushed, merged, tagged or deployed.  
**Branch:** `experiment/glon-https-g3a`  
**Worktree:** `/mnt/d/repos/glon-https-g3a`  
**Base:** G3 `32c0310` (implementation `9d943c7`)  
**Implementation commit:** `546de49`  
**Frozen S1:** `f90496c26dc45c7a387d8fc8639cd2781507d3d2` (unchanged)

G3A removes the artificial 7,232-cell managed-heap ceiling from general-purpose
server applications without changing the frozen S1 files. It adds a generated
large-memory profile, a reusable JSON vocabulary, a 10,240-slot membership
register with hash lookup and pagination, an NDJSON append-log persistence
layer, and reusable form/HTML helpers. The original constrained profile remains
the default and conformance target.

The long-running-server objective was investigated with a bounded endurance
harness. No long-term reliability claim is made; only the durations actually
completed are reported in §10.

---

## 1. Memory architecture

### 1.1 Before G3A

The reference machine is a flat `cell M[MEM_CELLS]` array with
`MEM_CELLS = 65536` in frozen `s1.h`, and `cell M[MEM_CELLS]` in frozen `s1.c`.
The 64K map is:

| Region | Cells | Notes |
|---|---|---|
| registers/scratch | 0–255 | S1 registers |
| emitted runtime code | 256–~24,576 | evaluator/collector |
| main data stack | 16,384 down | |
| main return stack | 24,576 down | |
| runtime state | 24,576–32,768 | RV/GC state, site table |
| managed heap | 32,768–40,000 | **7,232 cells** |
| loader heap | 40,000–54,800 | 14,800 cells |
| M1 arenas | 54,800–65,000 | 3 × 3,400 |
| task table | 65,000–65,536 | |

The 7,232-cell managed heap caused the G3 application to work around a three-
record capacity and frequent GC. The frozen array size was the hard
addressability limit; `cell` is `intptr_t` (64-bit native, 32-bit WASM), so the
runtime can represent much larger cell indices.

### 1.2 G3A large profile

The large profile keeps the frozen files untouched. The build generates:

- `build/profile/s1_profile.h` — a copy of `s1.h` whose **only** difference is
  `MEM_CELLS`;
- `build/profile/s1_profile.c` — a copy of `s1.c` whose include points at the
  profile header.

Those are build artifacts, not changes to `s1.c`/`s1.h`. The language, the seven
S1 instructions, the parser/evaluator and the collector logic are unchanged.
All other runtime sources continue to include the frozen `s1.h` for the
unchanged stack/heap-base constants.

The default large profile (G3A build) is:

| Region | Cells | Notes |
|---|---:|---|
| frozen low layout | 0–32,768 | code, stacks, runtime state |
| managed heap | 32,768–1,081,344 | **1,048,576 cells** (~8 MiB native) |
| GC worklist | 1,081,344–1,121,344 | 40,000 entries; bound is heap/32 = 32,768 |
| loader heap | 1,121,344–1,621,344 | **500,000 cells** for the G3A application |
| M1 arenas | 1,621,344–1,631,544 | 3 × 3,400, preserved |
| task table | 1,631,544–1,632,080 | 536 |
| generated `MEM_CELLS` | 1,633,104 | includes margin |

The G3A Makefile values are configurable:

```sh
make glon MODULES=file,http MEM_PROFILE=large \
  MEM_MANAGED_CELLS=1048576 MEM_WORKLIST_CELLS=40000 MEM_LOADER_CELLS=500000
```

`MEM_PROFILE=constrained` is the default and uses the original `s1.c` with no
generated profile. The large profile can also be built for 65,536, 262,144 or
other managed-cell counts by changing `MEM_MANAGED_CELLS` and
`MEM_WORKLIST_CELLS` (the worklist must be at least managed/32). The M1 arena
and task table are relocated above the loader, preserving task functionality;
the M1 regression test passes on the large profile (see §9).

`R0S1_HEAP_BASE` now means the managed-heap end; `R0S1_LOAD_BASE` is the
independent loader start, and `GC_WORKLIST` is profile-configurable. The old
`GLON_HTTP_RECLAIM_M1=1` workaround is not needed for G3A; it remains available
for the original G3 build but is not used by the G3A target.

### 1.3 Actual limits and recommendations

- Frozen `s1.c`/`s1.h` remain unchanged and `./check-frozen-s1.sh` passes.
- The 64K constrained profile remains the conformance/regression default.
- The large profile is a build-time experimental configuration. It changes
  memory capacity and addresses, not execution semantics.
- Native memory grows with `MEM_CELLS` (8 bytes/cell native; 4 bytes/cell
  WASM). The default large profile has a 13.2 MB BSS `M[]` array.
- The GC worklist is sized explicitly; a heap larger than 32× the worklist is
  refused by a build-time assertion before it can overflow.
- A runtime-selectable heap size was not introduced: the array size must be
  compile-time. The build options keep configuration simple and auditable.
- Addressability: cell indices are `intptr_t`; 1M and larger counts are
  representable. No references are truncated.

---

## 2. Garbage collection observations

The collector remains the existing non-moving, mark/sweep collector. No GC
redesign was undertaken.

Measurements:

| Workload | Managed heap | Requests | GC collections | Post-collection live cells |
|---|---:|---:|---:|---:|
| G3 acceptance | 7,232 cells | 327 | 254 | 4,240 |
| G3 acceptance | 1,048,576 cells | 327 | **0** | 0 (no collection ran) |
| G3A endurance | 1,048,576 cells | 1,010 | 3 | 26,672 |
| G3A endurance peak heap high | 1,048,576 cells | — | — | high water 1,081,344 |

The larger heap removes the allocation-pressure collections for the G3
workload. GC is still triggered by allocation pressure when the heap fills;
there is no evidence that larger heaps reduce the cost of an individual
collection, but they substantially reduce frequency. The heap is non-moving,
so fragmentation remains an architectural property. The G3A membership app
uses fixed slots, a cons-list overflow and part-based responses to reduce
fragmentation pressure; it is not a general fragmentation solution.

---

## 3. JSON vocabulary

`modules/json.glon` is a reusable, ordinary-Glon JSON vocabulary.

### 3.1 Value mapping

| JSON | Glon value |
|---|---|
| string | `STRING!` (UTF-8 bytes) |
| integer in Glon range | `INTEGER!` |
| fraction/exponent or out-of-range integer | `[ 'json-number "<exact token>" ]` |
| true/false | `[ 'json-bool 1 ]` / `[ 'json-bool 0 ]` |
| null | `[ 'json-null ]` (distinct from `none`) |
| array | `[ 'json-array <cons list> ]`, `none` for empty |
| object | `[ 'json-object <pair cons list> ]`, `none` for empty; key order preserved |

`json/object?`, `json/array?`, `json/null?`, `json/bool?`, `json/number?`,
`json/string?`, `json/integer?` are provided, plus `json/object-get`,
`json/array-values`, `json/number-text`, constructors and `json/decode` /
`json/encode`.

### 3.2 Parser behavior and limits

- RFC 8259 whitespace and value grammar; objects, arrays, nested values,
  strings, integers, fractions/exponents, booleans, null.
- Escapes `\" \\ \/ \b \f \n \r \t \uXXXX`; surrogate pairs become UTF-8;
  lone surrogates and malformed escapes are rejected.
- UTF-8 input is validated; raw control bytes in strings are rejected.
- Duplicate object keys are rejected.
- `json/max-depth` default 16; `json/max-items` default 32 per array/object.
  Larger collections must be paginated or line-oriented. This is an explicit
  resource boundary, not a claim of arbitrary JSON scale.
- Stable errors: `bad-token`, `unterminated-string`, `bad-escape`,
  `bad-unicode`, `bad-number`, `bad-utf8`, `too-deep`, `too-many-items`,
  `duplicate-key`, `trailing-data`; encoder `not-a-json-value`.
- The decoder uses private globals for its cursor and is not re-entrant.
- JSON is not implemented in C.

`make json-test` runs the independent suite (all value types, nested/empty
collections, null vs missing, escapes, surrogate pair, malformed input,
duplicate keys, bad UTF-8, limits, round-trips). Result: `JSON_TESTS PASS`.

---

## 4. Reusable web helpers

`modules/web.glon` separates generic form decoding and HTML escaping from the
membership application:

- `wp/form-scan body key 0` — first URL-decoded form value;
- `wp/form-check-loop body 0` — reject malformed percent escapes;
- `wp/escape-html s` — escape `& < > " '`;
- `wp/ci-prefix? s prefix` — case-insensitive content-type prefix check.

It contains no JSON parser. G3A uses `modules/json.glon` for all JSON.

---

## 5. Membership application

Files under `examples/https-g3a/`:

- `slots.glon` — 160 hash groups × 64 mutable slots (10,240 slots);
- `reg.glon` — record parsing/validation, FNV-style byte-sum hash, masm slot
  scans, overflow cons list, add/update/find, JSON member helpers;
- `persist.glon` — NDJSON append log, startup replay, seed, warnings/errors;
- `server.glon` — HTML/JSON routes, pagination, status.

Capacity is 10,240 fixed slots plus an overflow list. Listing is paginated by
hash group (page 0..159, page 160 = overflow), so no response returns the whole
register. HTML pages show ID and Name with a detail/edit page; JSON list/detail
are available. `/status` reports count, capacity, persistence state, warning,
error and path.

Validation remains intentionally small: required/non-empty, id <=64 bytes,
name/email <=128, no `|` or `;`, printable ASCII for names, an `@` in email,
and `active`/`inactive` status. Synthetic data only.

---

## 6. Persistence

`members.ndjson` in the process working directory is an append log of compact
JSON objects. On every add/update the app appends one object line with
`io/open ... 'append`, `io/write`, `io/close`. The host file pump drains the
asynchronous file queue after the HTTP response and at shutdown, so the request
does not block on the write.

At startup, before `http/listen`, the app reads the log in 2,048-byte chunks and
replays complete lines with `json/decode` and the same validation/insert path.
The last line for an id wins.

Guarantees and limits:

- A malformed complete line aborts startup before the listener starts and does
  not rewrite the file.
- A final partial line without a newline is treated as a truncated append and
  ignored; earlier valid lines load and `/status` reports a warning.
- The append log preserves previous valid bytes if a save fails or the process
  is interrupted mid-append.
- There is no fsync/durability guarantee; data is subject to OS cache and disk
  failure. There is no compaction, and the log grows with every update.
- There is no deletion operation; repeated updates reuse ids and append new
  lines. Deletion/tombstones remain future work.
- Persistence is enabled only when the app is built/run with the file module
  and the host grants read/write. The core HTTP module does not require it.

The G3A acceptance suite demonstrates 200 records surviving a restart, an edit
surviving restart, no duplicate seed, malformed-log abort without overwrite,
and truncated-tail recovery.

---

## 7. Scaling measurements

The scale harness pre-generates NDJSON and measures startup/replay, then a few
detail/list HTTPS requests. It does not claim HTTPS ingest throughput.

| Records | Startup/restore | Detail median | List median |
|---:|---:|---:|---:|
| 10 | 1.298 s | 104.9 ms | 147.1 ms |
| 100 | 18.438 s | 463.2 ms | 1186.9 ms |
| 1,000 | 450.143 s | 221.1 ms | 1298.3 ms |
| 10,000 | **failed during replay** | — | — |

These runs used the 512-byte replay chunk. The implementation was subsequently
changed to 2,048-byte reads (still bounded by the 512-value block-literal limit
through chunked `mk-string` replies). The 1,000/10,000 replay path is dominated
by the file module's one-request-per-chunk worker round trip, not by register
lookup. This is the measured bottleneck; no speculative optimisation was
applied. 1,000 records restores correctly. The 10,000-record replay was
attempted with the 2,048-byte chunk and failed with return-stack exhaustion
during line replay (`[dump] ... RP=16444`, followed by
`io/reply ... machine-level fail-stop`). It did not reach `http/listen`. The
160×64 slot table is also near full at 10k records, so overflow-list depth is a
second contributing limit. 10,000 is therefore a measured failure, not a
capacity claim.

---

## 8. Binary/source sizes and memory

Current G3A build: `MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000`.

| Item | Size |
|---|---:|
| `glon` unstripped | 444,768 B |
| `glon` stripped | 297,312 B |
| BSS (`M[]` + other) | ~13.2 MB |
| `modules/json.glon` | 26,752 B / 782 lines |
| `modules/web.glon` | 4,847 B / 172 lines |
| `slots.glon` | 53,752 B / 166 lines |
| `reg.glon` | 10,929 B / 315 lines |
| `persist.glon` | 5,995 B / 184 lines |
| `server.glon` | 19,031 B / 466 lines |
| G3A app source total | ~121 KB |

Peak RSS in the 1,000-request endurance run was 16,804 KiB (large heap plus
loader and app). The 100-request run peaked at 12,648 KiB. The original G3
server peaked at ~7.98 MiB on the 7,232-cell heap.

---

## 9. Regression results

Completed:

- `./check-frozen-s1.sh` — PASS.
- `make json-test` — `JSON_TESTS PASS`.
- `make io-file-c-test` — `PROTOCOL_TESTS_PASS`.
- G3A acceptance — `HTTPS_G3A_ACCEPTANCE PASS` (10 scenario groups).
- G3 acceptance on the 1,048,576-cell profile — `HTTPS_G3_ACCEPTANCE PASS`,
  327 requests, 0 GC collections.
- M1 multitasking tests on the large profile — `all R0-S1 M1 multitasking
  tests passed`, `M1_LARGE failures=0` (manual compile/run; same command is
  reproducible from the Makefile variables).
- `make https-g3-test` on the constrained/default profile — PASS before the
  G3A changes were layered; rerun in the final validation.

Not completed/claimed:

- A 100,000-request or 1,000,000-request endurance run.
- 10,000-record restore: attempted, failed with return-stack exhaustion during
  replay (see §7). The 1,000-record checkpoint is the largest completed restore.
- Full 10,000-record HTTPS ingest through individual requests.
- Sanitizer runs on the generated large profile (the normal C ASan probes are
  not wired to the profile build).

---

## 10. Endurance

`tests/https-g3a/endurance.py` uses keep-alive connections and a mixed workload
of list/detail/status/JSON-list reads, updates, invalid requests and 405s. It
samples RSS and file descriptors every 2 s and reports GC/live cells from the
shutdown report.

Actual completed runs:

| Run | Workers | Elapsed | Requests/s | Peak RSS | Live cells | GC | FDs | Log |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 requests | 1 | 34.04 s | 2.9 | 12,648 KiB | 0 | 0 | 5 → 4 | 1,549 B |
| 1,000 requests | 1 | 719.83 s | 1.4 | 16,804 KiB | 26,672 | 3 | 5 → 4 | 8,659 B |

The 1,000-request run had median latency 388 ms and p95 883 ms. A 4-worker
1,000-request attempt did not complete within its bound and was stopped; the
single CivetWeb worker serialises handlers, and multiple keep-alive connections
can starve one another. The 100,000/1,000,000-request objectives were **not**
completed. The measured bottleneck is per-request HTTP/TLS/file-pump latency
and the file module's one-chunk-per-worker round trip, not managed-heap
exhaustion in these runs. No long-term reliability is claimed.

Keep-alive is opt-in (`--http-keep-alive`); the default remains one response
per connection. Fresh-TLS and reused-connection measurements are therefore
separate: G3/G3A default fresh TLS is roughly 0.1 s per local request; the
keep-alive `/status` loop measured 50–135 ms per request on this host.

---

## 11. Known limitations

- The 64K constrained profile is still a supported but separate application
  budget; the 10k-slot membership app is intended for the large profile.
- Large memory is a build-time profile, not runtime-selectable.
- The managed heap is non-moving; fragmentation remains.
- The JSON decoder has depth 16 / items 32 limits and is not re-entrant.
- The membership log has no deletion, compaction or fsync.
- The file module's request/reply chunk round trip dominates large replays;
  1,000 records restored in 450 s in the measured run; 10,000 failed with
  return-stack exhaustion during replay.
- The single-worker server serialises Glon handlers; keep-alive concurrency can
  starve other connections. Do not raise CivetWeb worker count without an
  evaluator-ownership design.
- No authentication, authorisation, CSRF, sessions, rate limiting, static
  files, templates, WebSockets, HTTP/2/3 or public deployment.
- No object-start bitmap or exact activation roots were added; large-heap GC
  correctness is covered by the existing tests and M1 large test, not a new
  GC algorithm.
- Sanitizer coverage of the large generated profile is not yet wired.

---

## 12. Rejected alternatives

| Alternative | Reason |
|---|---|
| Change `MEM_CELLS` in frozen `s1.h`/`s1.c` | Violates the frozen-S1 rule; generated profile achieves the same without editing frozen files |
| Increase every allocation constant in place | No coherent profile; would break the 64K conformance target |
| Runtime `malloc`-backed heap | Would change frozen S1 addressing/semantics |
| C JSON parser | Moves application semantics into the host; ordinary Glon is sufficient at the configured limits |
| General dynamic block append | No existing primitive; fixed slots/hash and cons lists meet the demonstrated app scale without a core change |
| Full-file JSON persistence | 10k records plus encoded document exceed the in-memory/temporary budget; NDJSON append streams |
| Multi-worker CivetWeb handlers | The Glon evaluator is not concurrently re-entrant; would require an evaluator-ownership design |
| Periodic restarts as a memory strategy | Explicitly rejected by the long-running-server objective |

---

## 13. Recommended subsequent work

1. **Streaming/bulk file I/O**: remove the one-request-per-chunk replay
   bottleneck (bulk read, larger reply frames, or a line-oriented host
   callback). This is the largest measured limit on 10k restore.
2. **Deletion/tombstones and log compaction** for lifecycle endurance, plus an
   explicit fsync/durability option.
3. **JSON collection strategy**: dynamic block append or a streaming array
   representation, then raise the 32-item limit and add more RFC 8259
   conformance vectors.
4. **Endurance optimisation**: reduce keep-alive/CivetWeb polling latency,
   measure 100k and 1M requests, and add sustained concurrent connection tests.
5. **GC**: measure large-heap mark/sweep cost and fragmentation; consider the
   object-start bitmap already proposed in `GLON-MEMORY-PROFILES.md`.
6. **Memory profile policy**: decide whether the large profile becomes a
   documented server default, and how to expose 262K/1M choices.

---

## 14. Files changed in G3A

| File | Change |
|---|---|
| `r0_s1.h` | configurable heap/loader/worklist bases; default 64K unchanged |
| `r0_s1_runtime.c` | loader base and worklist-region range checks |
| `Makefile` | memory profile generation/options, JSON/G3A/M1 targets |
| `glon.c` | file pump for HTTP, chunked file reply source, keep-alive option |
| `glon_http.c/.h` | keep-alive, file-pump callback |
| `glon_io_file.c/.h`, `glon_io_file_posix.c` | append mode, larger bounded chunks |
| `modules/file.glon` | `'append` mode |
| `modules/json.glon` | reusable JSON vocabulary |
| `modules/web.glon` | reusable form/HTML helpers |
| `examples/https-g3a/` | scaled register, persistence, server |
| `tests/json/` | independent JSON suite |
| `tests/https-g3a/` | acceptance, scale and endurance harnesses |
| `tests/m1_large_driver.c` | M1 large-profile regression driver |

Frozen `s1.c`, `s1.h`, `tests.c`, `adversarial.c` and `claims.c` are untouched.
