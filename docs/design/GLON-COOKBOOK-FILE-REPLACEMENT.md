# Cookbook 0004 — complete file text replacement

**Status:** completed local integration. Not pushed, merged, tagged or deployed.
**Branch:** `integration/cookbook-file-replace`
**Worktree:** `/mnt/d/repos/glon-cookbook-complete`
**Base:** `87a5ca70610585df387a4a097541b87c4d82e3f2` (MASM-3)
**Implementation commit:** `a2245b2b6e5854bcbaf21406b9b852d84427d5f4`.
**Language/grammar source commit:** `ceebc62cfde847530269ff4f8bd60e7739cc8054`.
**Final integration revision:** this commit (the engineering branch head; the
exact hash is pinned in `gchiu/glon/interpreter.json`).

This completes REBOL Cookbook recipe 0004 at the original path:

```rebol
file: %doc.txt
text: read file
replace text "web" "rebol"
write file text
```

The application-level Glon equivalent is:

```glon
cookbook/replace-first: func [] [
    file/replace-first "doc.txt" "web" "rebol"
        func [done] [ s/print "COOKBOOK_DONE" ]
        func [error] [ raise error ]
]
```

No frozen S1 file, HOST id, parser/evaluator semantic, GC or M1 scheduler was
changed. The replacement algorithm remains Glon; the host only performs
general-purpose filesystem operations.

---

## 1. Restored context and selection

The IO-1/IO-2/IO-3 reports, the HOST investigation, MASM-1, IO-4, MASM-2 and
MASM-3 reports were reviewed, and the OpenViking engineering-memory entries
were checked. The experimental baselines remain unchanged.

Components considered:

| Component | Decision | Reason |
|---|---|---|
| Optional async file module (IO-1/2) | **Integrate** | The challenge needs file I/O and the asynchronous service model. The host gains general temp/commit/discard operations, not replacement logic. |
| IO-3 streaming replacement | **Reuse the algorithm** | Bounded 400-byte chunks; the in-place operation drives the same read/replace/write loop to a temporary output handle. |
| MASM-1 `s/find` | **Integrate** | Small (241 cells / 1,928 bytes), 221/221 differential, strong search acceleration; used through the existing `stream/find-first` override. |
| MASM-2 `io/u32` | **Integrate** | Fixed-size managed encoder, 217/217 differential, validated allocation/lifetime tests; reduces request-field allocation in every I/O turn. |
| MASM-3 `s/slice2` | **Do not integrate** | Modest matched/boundary gain and approximately neutral no-match performance in MASM-3; the extra variable-length/workspace convention is not justified for this completion. |
| Profilers, benchmark C, unused helper closures | **Exclude** | Not part of the application runtime. The deployment uses the minimal MASM libraries and `file-replace.glon`/`file-atomic.glon`. |

The application runtime therefore loads the existing MASM-1 find, MASM-2 u32,
the MASM-1 streaming-search override, and the new reusable file modules. The
host has no text replacement code.

---

## 2. Host protocol v2: general filesystem operations

`glon_io_file.h` keeps request magic `0x01` and reply magic `0x81`. Protocol
version is now 2; the change is **additive**:

| Op | Name | Fields | Reply |
|---|---|---|---|
| 1 | open | name, mode byte | two u32 handle values |
| 2 | read | handle, max u32 | bytes |
| 3 | write | handle, bytes | written u32 |
| 4 | close | handle | none |
| 5 | temp-open | source logical name | two u32 handle values |
| 6 | commit | temp handle, target logical name | none |
| 7 | discard | temp handle | none |

Old clients never send 5–7; an old host returns `unsupported`. Existing
operations are unchanged. The host never sees a search pattern, replacement
text or Glon semantics.

### 2.1 Temporary file

`temp-open source`:

- validates the logical name and resolves the existing source under the
  canonicalised `--io-root`;
- rejects a final-component symlink (`lstat` + `O_NOFOLLOW`);
- requires a regular file;
- copies the source `st_dev`, `st_ino`, `st_size`, `st_mtim`, mode and
  directory into the handle slot;
- creates `.<...>/.glon-tmp-<pid>-<counter>-<random>` in the source directory
  with `O_CREAT|O_EXCL|O_NOFOLLOW`, mode `0600` (a unique file, not a predictable
  name);
- returns an open write handle.

The file stays private while it is being written. The source permission bits
are applied only at commit, after all bytes are written and the source has been
checked.

### 2.2 Commit

`commit temp target`:

1. requires the write capability;
2. resolves `target` under the root and requires the same real directory as
   the temporary (same filesystem; atomic rename is meaningful);
3. rejects a symlink or non-regular existing target;
4. re-stats the source and compares device, inode, size, mtime and mode with
   the values captured at `temp-open`; a difference returns `stale`;
5. closes the temporary (a close error fails the commit);
6. applies the source permission bits to the temporary;
7. `rename(temp, target)`.

On any commit failure the temporary is unlinked and the original path is left
untouched. On success the temp path is cleared so cleanup cannot remove the
committed target. `discard` closes and unlinks an unfinished temporary. Service
cleanup/destroy also unlinks any temp handle left open.

### 2.3 Security, links and failures

- **Confinement:** all names use the existing logical-name validator; absolute,
  `..`, empty components, backslashes and drive prefixes are rejected. Source
  and target are resolved with `realpath` and must remain under `--io-root`.
- **Symlinks:** a symlink source is rejected; a symlink target is rejected;
  `O_NOFOLLOW` is used for create/open. TOCTOU caveats of the existing host
  policy remain (best-effort POSIX confinement, not a hostile multi-process
  sandbox).
- **Hard links:** replacement renames a new inode over the original path.
  Other hard links continue to reference the old content. This is documented
  and tested; it differs from REBOL's in-place truncate/write.
- **Permissions:** source mode bits are preserved. Ownership, group, ACLs,
  xattrs and timestamps are not preserved.
- **Concurrent modification:** best-effort detection at commit using
  device/inode/size/mtime/mode. Content changes with unchanged metadata are not
  detected; the source is not locked.
- **Cancellation:** cancellation before a request is executed produces no reply
  and the open temp is removed at cleanup. A cancellation that races an
  already-executing commit may allow the single `rename` to complete; the result
  is a complete new file, never a partial one.
- **Failure before/during:** the original path is not replaced. The temporary
  is removed where possible. Write/close/rename injection tests cover the
  paths.
- **Crash durability:** `rename` is atomic with respect to concurrent readers.
  This is **not** a power-loss durability claim: neither the temporary file nor
  its directory is fsynced. A crash may leave a `.glon-tmp-*` file and may lose
  the new version according to filesystem policy. No atomic-replace or
  durability claim is made beyond process-level operations.

Test-only flags `--io-fail-close` and `--io-fail-rename` inject the respective
failures; they are not production policy.

---

## 3. Reusable Glon vocabulary

`modules/file-replace.glon` (148 lines / 6,633 bytes):

- `s/replace-first text old new` — immutable in-memory first-occurrence
  replacement. Empty `old` raises `SIN! 's 'empty-pattern`.
- `stream/replace-first-to in out old new success failure` — the same
  streaming algorithm into an already-open write handle. Input is closed by
  the streaming finish; output remains open for commit/discard. Empty `old`
  raises `SIN! 'stream 'empty-pattern`.
- `file/replace-first path old new success failure` — opens the source read,
  creates the same-directory temporary, streams the replacement, commits the
  temporary over `path`, discards on failure. Empty `old` raises
  `SIN! 'file 'empty-pattern`.
  Success receives `NONE`; failure receives a `SIN!` value.

`modules/file-atomic.glon` (31 lines / 1,295 bytes) provides the low-level
`io/temp-open`, `io/commit`, `io/discard` wrappers. It is intentionally **not**
loaded by `MODULES=file` by default: the existing whole-file 1,024-byte recipe
depends on the base module's heap/code budget. Applications that need safe
replacement load it explicitly.

Contracts:

- arbitrary permitted logical filenames, patterns and replacements (not
  hard-coded to `doc.txt`/`web`/`rebol`);
- immutable STRING! arguments; no implicit binary/string conversion;
- only the first occurrence changes;
- no match still writes a complete new file (matching REBOL's `write`), but
  atomically;
- asynchronous completion: callbacks run in later reply turns; the call itself
  returns immediately.

---

## 4. Application-level source

`challenges/file-replace/replace.glon` in the public repository contains only
definitions (the public runner appends case calls):

```glon
cookbook/replace-first: func [] [
    file/replace-first "doc.txt" "web" "rebol"
        func [done] [ s/print "COOKBOOK_DONE" ]
        func [error] [ raise error ]
]
```

Application source: **5 executable lines**; the published file with explanatory
comments is **18 lines / 826 bytes** (the site generator reports the full file).
This is not compared as a concision contest with the four-line REBOL
application; the layered comparison is maintained in the public challenge.

---

## 5. Tests and results

### 5.1 Host protocol tests

`make io-file-c-test`: **PROTOCOL_TESTS_PASS (0 failures)**, including new
temp/commit/discard tests:

- exclusive temp creation; write/commit replaces the original; committed
  handle invalid;
- discard removes the temp and preserves the original;
- stale source rejected and temp removed;
- cross-directory commit refused and temp removed;
- symlink source rejected;
- permission bits preserved;
- service cleanup removes an unfinished temp and preserves the original.

### 5.2 In-place acceptance

`make io-file-inplace-test`: **36 passed, 0 failed**, expected values from
Python `bytes.replace(old, new, 1)`:

- Cookbook example; beginning/middle/end; multiple matches (first only); no
  match; empty file; empty replacement; shorter/longer replacement;
  replacement containing the pattern; empty search rejected;
- binary content, NUL/high bytes, CR/LF; binary pattern/replacement;
- matches crossing the 400-byte boundary and in the carried tail;
- short reads/writes;
- missing file, read/write capability denial, out-of-root path;
- host-cap write failure, injected close failure, injected rename failure,
  cancellation;
- temporary-creation failure (read-only directory);
- symlink source rejected with target untouched; hard-link behaviour;
- concurrent source modification detected, source preserved, temp removed;
- 1 KiB and 64 KiB files;
- reusable in-memory and streaming-to layers separately.

### 5.3 Existing regressions

| Test | Result |
|---|---|
| `./check-frozen-s1.sh` | PASS (`f90496c…`) |
| `make io-file-test` (whole-file acceptance) | 30 passed, 0 failed |
| `make io-file-stream-test` | PASS, 0 failures |
| MASM-1 `cases.py` | 221/221 differential, immutability, invalid type |
| MASM-2 `u32_cases.py` | 217/217, allocation pressure |
| MASM-3 `slice2_cases.py` (baseline not integrated) | all PASS |
| IO-4 loader lifetime | PASS |
| Non-simulation native runner | 948 checks, 0 failures |
| Native smoke (minimal and `MODULES=file`) | PASS |
| WASM tests | not run: `emcc` unavailable |
| Full `./s1` | not claimed; documented long simulation stalls remain |

### 5.4 Public challenge tests

The public repository is updated separately. Its runner builds the pinned
engineering commit with `make glon MODULES=file`, supplies per-case temporary
working directories and `--io-root`, verifies expected file bytes and temp
cleanup, and preserves GCD/factorial results. Actual public results are
recorded in its `results/file-replace.json` and generated page; none are
hand-written.

---

## 6. Performance and resource measurements

Matched native `-O0` build (`make glon MODULES=file`), WSL2 host; wall times are
load-sensitive. Expected output verified for every row; no temp left.

| File | Case | Seconds | GC collections | Post-collection live cells | Peak RSS |
|---:|---|---:|---:|---:|---:|
| 1 KiB | match | 0.10 | 1 | 3,648 | 2,336 KiB |
| 1 KiB | no match | 0.09 | 1 | 3,952 | 2,528 KiB |
| 64 KiB | match | 3.20 | 82 | 3,680 | 2,272 KiB |
| 64 KiB | no match | 3.56 | 82 | 2,608 | 2,272 KiB |
| 1 MiB | match | 55.28 | 1,311 | 2,624 | 2,136 KiB |
| 1 MiB | no match | 51.10 | 1,311 | 2,640 | 2,336 KiB |
| 4 MiB | match | 203.19 | 5,243 | 2,464 | 2,140 KiB |
| 4 MiB | no match | 195.44 | 5,243 | 2,592 | 2,464 KiB |

- Managed heap capacity: **7,232 cells** (fixed 64K map).
- Post-collection live memory stays roughly **2.5–4.0k cells** from 1 KiB to
  4 MiB; this is the measured post-collection figure, not an instantaneous
  peak-live measurement.
- GC collections scale with bytes processed (1,311/MiB; 5,243/4 MiB), consistent
  with the existing streaming model.
- Peak RSS stays around **2.1–2.5 MiB** across all tested sizes.
- Optional module binary overhead on this build: **185,224 bytes minimal →
  250,680 bytes `MODULES=file` (+65,456 bytes; text +31,701 bytes)**. The
  accelerated vocabulary is MASM-1 search 241 cells/1,928 bytes and MASM-2 u32
  162 cells/1,296 bytes; the new reusable Glon libraries are 6,633 + 1,295
  bytes of source.

No further optimisation was undertaken. IO-5 worker signalling, GC work and
larger chunks remain parked.

---

## 7. Remaining limitations

- POSIX-only optional module; no WASM/Windows/embedded file backend.
- No power-loss durability; no fsync of file or directory.
- Ownership, group, ACLs, xattrs and timestamps are not preserved.
- Hard links other than the replaced path keep the old content; symlink final
  components are rejected rather than followed.
- Concurrent modification detection is metadata-based and best-effort; there is
  no locking or content hash.
- Cancellation survives at request boundaries; a commit already executing may
  complete, but only atomically.
- Temp files left by a forcibly killed process are not removed by that process.
- The managed heap bounds variable in-memory values; streaming keeps live memory
  bounded but GC time still scales with file size.
- Full `./s1` still stalls in the documented long simulation tests; the
  non-simulation 948-check runner passes.
- WASM tests were not run because `emcc` is unavailable.

---

## 8. Publication steps

1. Engineering commit on `integration/cookbook-file-replace`; report the exact
   hash.
2. Push the engineering branch/revision and make it publicly accessible.
3. Update `gchiu/glon` on `feature/file-replace-complete`: pin that exact
   engineering commit, build with `MODULES=file`, run all challenges, build the
   site, commit.
4. Push the public branch/merge to `main`; verify CI and the deployed page.
5. No push occurs before explicit approval.
