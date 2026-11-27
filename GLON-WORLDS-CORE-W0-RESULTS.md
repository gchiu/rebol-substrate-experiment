# GLON-WORLDS-CORE-W0 — Restore Glon Worlds on C2

Date: 2026-11-27
Branch: `integration/glon-worlds-core-w0`
Worktree: `/home/developer/projects/glon-worlds-core-w0`
Base: C2 `4dcae2aa10e275f23e7b2c1904fa45f8b888e243`
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: integrated, tested and committed locally on the integration branch.
Not pushed, merged, rebased or deployed. No authentication or other new
application feature was started.

**Result:** the previously verified HTTPS, cooperative-task, JSON J1/J2 and G3H
SQLite registry work now runs on the C2 core. C2's profile generator, layout
check and C1 capacity-dependent global-context hash are retained. Frozen S1 and
the seven-primitive interface are unchanged. G3H runs in both direct mode and
task mode with the existing 3 × 3,400-cell task stacks; all supported routes
respond and SQLite state persists across requests and restarts.

## 1. Source history and migration map

The old HTTPS/application line and C2 share the D1 merge base `3a194a2`. C2 is
`3a194a2 → C1 df15c05 → C2 4dcae2a`. The feature line is
`3a194a2 → … → G3H 56b7cc2 → … → H3E 518bb8c → J1 daa444a → J2 e80bd06`.
The old branch was not merged wholesale.

| Source work | Commit | Brought forward as |
|---|---|---|
| G3C security primitives | `40e6fcd` | `glon_sec.c/.h`, `modules/sec.glon`, `tests/security-g3c` |
| G3D SQLite module | `b9fb4f8` | `glon_sqlite.c/.h`, `modules/sqlite.glon`, `tests/sqlite-g3d`, file-backend path resolver |
| G3B Glon Worlds app | `239edbf` | `examples/glon-worlds-g3b/server.glon` (final G3H version) |
| G3F bounded HTTPS workers | `5205806` | final `glon_http.c/.h` worker-pool ownership |
| M2 loadable tasks | `73c1384` | `modules/task.glon`, runtime raw symbols, task raw symbols in `m1_layout.h` |
| M3 multi-outstanding I/O | `9060662` | final `modules/file.glon` queue |
| M4 reusable tasks | `7aa46aa` | `TREC_GEN`, slot-reuse generation handling |
| H2A/H2B/H2D bridge | `918b94c`, `ee36a81`, `6ff2618` | final bridge/task paths in `glon_http.c`, `modules/http.glon`, `TREC_RESP` GC root |
| H2Z HTTP-word fix | `c3239e2` | final `modules/http.glon` |
| H3A cooperative HTTPS+SQLite | `660cce7` | subsumed by final `glon_http.c`/`modules/http.glon` |
| H3C method propagation | `777f244` | final `glon_http.c`, H3C regression, `examples/h2b/tasks.glon` |
| G3G access log | `8cf66c4` | final `glon_http.c/.h`, `glon.c`, G3G regression |
| G3H SQLite user registry | `56b7cc2` | final Glon Worlds app and `tests/glon-worlds-g3h` |
| H3E response lifecycle | `518bb8c` | final `modules/http.glon` and `glon_http.c`, H3E regression |
| JSON J1 encoder | `daa444a` | final `modules/json.glon`, `tests/json-j1` |
| JSON J2 decoder | `e80bd06` | final `modules/json.glon`, `tests/json-j2` |

**Deliberately excluded:** the S1-D/H2 diagnostic/trace commits
(`d80cc8c`…`c3fcf64`), `HOST_TRACE_FRAME`, the H2S/H2V/H2Y debug capture and
task dumper, `--task-dump`, the mem-M* profile experiments, the G3I/G3J
authentication work and its symbol-capacity experiment. C2 already contains the
C1 hash/return-stack corrections (`1512bbb`, `73fcc44` equivalents); the old
copies were not reapplied.

The ported C/H files match the final `e80bd06` versions except for removal of
the excluded diagnostic entry points. C2's `r0_s1.h`, profile system and layout
check were kept; only the feature symbols/fields were added to
`r0_s1_runtime.c` and `m1_layout.h`.

## 2. Profiles used

No profile capacity was changed.

| Purpose | Profile | Relevant capacities |
|---|---|---|
| H3E, G3H task mode, JSON J2 G3H, W0 route suite | `server-https` | 1,633,104 cells; code cap 20,480; main RS 4,096; managed heap 1,048,576; worklist 40,000; loader 500,000; 3 × 3,400 tasks; globals 512; symbols 1,024; radix 512 |
| G3H direct mode, G3G | `server-https` | same |
| JSON J1/J2 and existing JSON suite | `large` | 1,333,104 cells; code 16,384; RS 8,192; heap 1,048,576; worklist 40,000; loader 200,000; 3 × 3,400 tasks; 256/512/256 |
| G3A regression | `desktop-1m` | 1,048,576 cells; code 16,384; RS 8,192; heap 262,144; worklist 8,192; loader 734,736; 3 × 3,400 tasks; 4,096/8,192/4,096 |

**G3A profile note.** The restored HTTP vocabulary now exceeds the `server`
profile's 512-symbol table at G3A startup. The existing `desktop-1m` profile is
used for the G3A regression instead of adding or enlarging a capacity.
`server-https` was not suitable for the G3A app; `desktop-1m` retains the
8,192-cell main return stack the G3A app uses. This is a profile-selection
consequence of integration, not a layout or capacity change. The H3D
investigation checkpoint (`54315d1`) was not separately ported; its final
loader-reclamation behaviour is part of the H3E source.

## 3. Tests run

All builds and tests were run sequentially with bounded timeouts.

### W0 acceptance

| Test | Result |
|---|---|
| Frozen S1 | **PASS**, `f90496c…`, all frozen files unchanged |
| `make json-j1-test` | **PASS (73 checks)** |
| `make json-j2-test` | **PASS (96 checks)** |
| `make json-j2-g3h-test` | **PASS (18 checks)**; escape-heavy 386-byte body, 10 malformed bodies, no halt |
| `make h3e-test` | **H3E ACCEPTANCE PASS** (immediate, async, sequential, overlap, failures, slot capacity, disconnects, after-response, bookkeeping) |
| `make h3c-test` | **H3C ACCEPTANCE PASS** (method echo, SQLite effect, three-task interleaving) |
| `make g3h-test` | **G3H SMOKE PASS** (create/get/disable users, duplicate 409, message routes, logging, restart persistence, non-loopback 403) |
| `make g3g-test` | **G3G SMOKE PASS** (logging, rotation, restart) |
| `make w0-routes-test` | **W0_ROUTES PASS**; G3H in task mode with 3 × 3,400 stacks (`tests/glon-worlds-w0/routes_task.py`) |
| `make sqlite-test` | All SQLite service tests passed |
| `make sec-test` | All security primitive tests passed |

`w0-routes-test` starts G3H with `--http-tasks` and verifies:
`GET /api/groups`, `GET /api/session`, `GET /api/users`, `POST /api/users`,
`GET /api/users?id=N`, `POST /api/users/disable`, duplicate-username 409,
`GET /api/messages`, `POST /api/messages`, `GET /api/messages` with a stored
message, and unknown-route 404. It then restarts the server and verifies the
SQLite user/status records and the NDJSON message persist. The server log
contains no machine halt.

### C2 regressions

| Test | Result |
|---|---|
| `make arch-check` | **PASS**, constrained map unchanged |
| `make profile-check` | **PASS (8 profiles)**; generated constrained equals the built-in 64K defaults |
| `make profile-reject-check` | **PASS (15 rejection cases)** |
| `make profile-common-test` | **PASS (8 profiles)** |
| `make build-deps-check` | **PASS (30 checks)** |
| `make global-ctx-hash-test` | **PASS on all 8 profiles** |
| `make m1-large-test MEM_PROFILE=large` | **PASS**, `M1_LARGE failures=0` |
| `make json-test` | **JSON_TESTS PASS** |
| `make https-g3a-test` (desktop-1m) | **HTTPS_G3A_ACCEPTANCE PASS** |

All eight profile `layout.txt` maps were parsed again: regions are ordered, no
region overlaps its predecessor, and every map ends within `MEM_CELLS`.

## 4. Known defects outside W0 (documented, not fixed)

These are preserved from the source work and are not caused by the C2
integration:

1. **Recursive HTML escaping** in `modules/web.glon` recurses per character;
   a long task-mode message can exhaust a task's return stack. G3H allows 1,024
   bytes but the defect remains. W0's route test uses a short message.
2. **HTTP request-body bridge failure at roughly 500 bytes** in task mode; the
   bridge embeds the body in parsed source and can exceed the parser block
   limit. W0 tests stay within the bridge limit.
3. **Heavily escaped JSON decodes slowly** (interpreter `-O0`, about 4 ms per
   escape); bounded stack does not mean bounded time.
4. **JSON encoder/decoder nesting-limit mismatch**: decode accepts 16 levels,
   encode advances depth by 2 per level and rejects above 8.
5. **No isolation for fatal task failures**: a machine fail-stop inside a task
   still stops the server; only judgeable SIN!s are isolated.

## 5. Remaining limitations

- H3E task-slot/response limitations documented upstream remain: an
  already-answered task that never finishes keeps its slot; responses for
  closed/duplicate ids are dropped silently; only the owning task should answer
  in practice; client disconnect is detected at deadline/reply failure.
- G3H requires `server-https` in task mode because the restored vocabulary
  needs 1,024 symbols. The `server` profile remains the G3A-era 512-symbol
  configuration.
- Windows, macOS, Android, ARM64 and WASM builds were not exercised. No
  cross-platform acceptance is claimed.
- No authentication, sessions, CSRF, rate limiting or public deployment
  hardening is included; authentication was explicitly out of W0 scope.
- The old experimental diagnostics and memory-profile experiments were not
  imported.
- No push, merge, rebase, tag or deployment was performed. C3, vocabulary
  integration, authentication and Glon Worlds feature work were not started.
