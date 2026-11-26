# G3J.4 — Isolate Glon Authentication Login Failure

Date: 2026-11-26
Branch: `experiment/glon-http-g3j4`
Baseline: G3J.3, commit `f30d61d`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Reproduction

`POST /api/auth/setup` (correct) → `{"user_id":1}`.
`POST /api/auth/login` (correct password) → **500 "Glon security operation
failed"**. Server stays alive (no halt).

## 2. Diagnosis (which sec op fails)

The host's security pump fails (`drain_sec` < 0 → "security pump failed" →
`http.fatal` → 500). The underlying error, captured from the server log:

```
glon: pre-service pump: sec field is not a STRING!
```

`take_sec_request` (glon.c:840) rejects a sec field that is not `T_STRING`.
Minimal type instrumentation in the login path (temporary, removed) showed:

```
DBG saltstr=0 nint=1
```

i.e. the **stored salt is NOT a string** (`s/raw-string? salt` = 0), while `n`
is a valid int. The salt is stored as a **BLOB** (`sqlite/bind-blob`), and the
SQLite module returns BLOB columns as a **type-3 (non-string)** value
(glon_sqlite.c:287 encodes `SQLITE_TEXT ? 2 : 3`). `sec/scrypt` requires string
fields, so it fails with "sec field is not a STRING!".

**Why setup works but login fails:** setup uses a *fresh* `sec/random` salt (a
string) directly; login reads the salt back from SQLite, where the BLOB round-trip
yields a non-string.

**Attempted TEXT fix (rejected):** storing the salt/hash as `bind-text` makes
them strings, but the random 16/32 bytes are not valid UTF-8, so the next
failure is `json bad-utf8` (the non-UTF8 bytes reach a JSON path). So neither raw
BLOB nor raw TEXT works for binary values.

## 7. Minimal reproducer (the salt round-trip)

```
sec/random 16 func [salt] [                       ;; salt: STRING (raw bytes)
  sqlite/bind-blob st 1 salt ...                  ;; stored as BLOB
  sqlite/step ... func [row] [
    v: block-at row 0                             ;; v: type-3 (NOT a string)
    s/raw-string? v                               ;; => 0
    sec/scrypt "pw" v 16384 8 1 32 ...            ;; => "sec field is not a STRING!"
  ]
]
```

This is the smallest program that reproduces the failure: a binary salt stored as
BLOB and read back is not a `STRING!`, and `sec/scrypt` rejects it.

## 4. Fix direction (identified, not completed in this milestone)

Store the binary values as **hex strings** (valid ASCII), which are `STRING!`:

- `salt`: `sec/hex (sec/random 16)` → a hex string; use that hex string **as the
  scrypt salt** (the salt is arbitrary bytes, so hex chars are valid salt bytes —
  no decode needed). Store the hex string (TEXT).
- `hash`: `sec/hex (scrypt ...)` → hex string; store hex; at login recompute and
  `sec/hex` then `sec/ct-eq` the two hex strings.
- `token_hash` / `csrf_hash`: store `sec/hex (sec/sha256 …)`; look up by the hex
  string.

This keeps every stored value a valid `STRING!`, avoids the BLOB/non-string and
the non-UTF8/TEXT problems, and needs no hex-decode (the salt is used as-is).

A first-pass implementation of this was attempted but the added `sec/hex`
nesting made the generated source bracket-mis-nested (load-time fail-stop); it was
reverted to keep the tree clean. The fix is mechanical but needs careful bracket
management.

## 5. Status vs criteria

- Login failure isolated to the BLOB salt (non-string) → sec/scrypt — DONE.
- Minimal reproducer (salt round-trip) — DONE (Section 3).
- Fix applied and login verified — **NOT DONE** (hex encoding identified; the
  first implementation attempt was reverted after a bracket error).
- Full sequence (create/set/login/session/logout) — **BLOCKED** by the above.
- Frozen S1 / G3H regression — unaffected (no runtime change).

## 6. Recommendation

Implement the hex-encoding of salt/hash/token_hash/csrf_hash carefully (the
direction is demonstrated). No runtime, S1 or auth-design change is needed; it is
an application-level encoding fix. The `sqlite3_exec` ≥3-statement defect (G3J.3)
and the BLOB-vs-string issue (this milestone) are the two demonstrated blockers.

## 7. Commit

Branch `experiment/glon-http-g3j4`, one local commit. Not pushed/merged/deployed.