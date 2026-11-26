# G3J.7 — Simplify Glon Worlds Authentication (blocked at loader behaviour)

Date: 2026-11-26
Branch: `experiment/glon-http-g3j7`
Baseline: G3J.6 working tree (g3j5 `1fa7539` + uncommitted diagnostics)
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Baseline / preservation

- Branch `experiment/glon-http-g3j7` created from the G3J.6 working tree.
- Preserved: `H3B-RESULTS.md` (untracked), `tests/g3j/repro_lookup.glon`,
  and the existing `server-auth-attempt.glon` diagnostics.
- Saved an investigation reference **without overwriting**: 
  `tests/g3j/server-auth-attempt-g3j6-reference.glon` (copy of the G3J.6 source).

## 2. Loader behaviour (verified)

- **Library → app works:** `./glon glon-lib/strings-ext.glon app.glon` where the app
  calls `s/length` → prints `len=5`. Definitions from a loaded library are visible
  to the app.
- **App → app fails:** `fa.glon` = `[ foo: 7 ]`, `fb.glon` =
  `[ s/print s/from-int foo ]` → `./glon fa.glon fb.glon` → **machine fail-stop**
  (`word=foo` unbound). A second app file cannot see the first app file's
  definitions.

Minimal reproducer:
```
# fa.glon
[ foo: 7 ]
# fb.glon
[ s/print s/from-int foo ]
$ ./glon fa.glon fb.glon   # -> halted (word=foo)
```

So splitting the authentication application into **multiple app files** that share
definitions is **not supported** by the current loader: only library files
(loaded before the app) publish their words to the app; app-to-app sharing does
not.

## 3. Consequence for the refactor

The requested decomposition (user-db / password / session / handlers / startup as
separate files that reference each other's words) cannot be done as separate
**app** files. It would require either:
- building the components as **library** files (loaded before the app, e.g. beside
  the executable / via the module mechanism), or
- a loader change to share app-file definitions (out of scope — no runtime change).

This is a reproducible component-level blocker, so per the task I stop rather than
redesign the loader.

## 4. Authentication cycle

**Not completed.** The G3J.6 blocker remains: inside `auth/setup`, `db/user-by-name`
fails regardless of a literal or JSON-derived username (literal → uncaught
"internal error"; JSON-derived → "lookup failed"), while the identical query
succeeds in the standalone `repro_lookup.glon`. The refactor was blocked before it
could be applied, and the underlying setup-context failure is unresolved.

## 5. Status vs criteria

- Preserve G3J.6 diagnostics + repro + H3B — DONE.
- Save reference without overwrite — DONE.
- Verify multi-file loading — DONE (lib→app works; app→app fails — blocker).
- Refactor into smaller app modules — **BLOCKED** (app-to-app sharing unsupported).
- `db/user-by-name` succeed in setup / full auth cycle — **NOT DONE** (G3J.6
  context blocker; refactor blocked).
- Frozen S1 / SQLite / auth design / symbol capacity — unchanged.

## 6. Recommendation

Two separate issues must be resolved before G3J can complete:
1. **Loader:** app-file definitions are not shared across files (only libraries are).
   The auth decomposition should target **library** files, or the loader needs a
   reviewed change.
2. **Setup context:** `db/user-by-name` fails only inside `auth/setup`'s nested
  callback context (not in a minimal handler). This needs a focused runtime/context
  investigation (the value is a valid string; `sin-id` on the failure raised).

No push/merge/deploy. Commit is local on `experiment/glon-http-g3j7`.