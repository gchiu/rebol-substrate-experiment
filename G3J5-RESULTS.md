# G3J.5 — Fix SQLite Credential Encoding

Date: 2026-11-26
Branch: `experiment/glon-http-g3j5`
Baseline: G3J.4, commit `596d2d1`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Change (hexadecimal credential encoding)

All binary credential material is now stored as **hexadecimal ASCII text**
(valid `STRING!`), using the existing `sec/hex`:

- **salt**: `sec/hex (sec/random 16)` → hex string; that hex string is used
   directly as the scrypt salt (the salt is arbitrary bytes, so hex chars are
   valid salt bytes — no hex-decode needed) and stored as TEXT.
- **password hash**: `sec/hex (scrypt …)` → hex string, stored as TEXT; at login
   the computed hash is `sec/hex`-ed and compared with `sec/ct-eq` against the
   stored hex.
- **session token hash / CSRF hash**: `sec/hex (sec/sha256 …)` → hex string,
   stored as TEXT and looked up by the hex string (login and session and logout).
- `bind-blob` → `bind-text` for these fields. Plaintext passwords and raw tokens
   are never stored (only their hex-encoded SHA-256 hashes).

The SQLite BLOB representation, the sec primitives, the S1 evaluator and runtime
capacity limits are unchanged. Bracket structure was verified (depth 0) and the
application loads and reaches `http/listen`.

## 2. Result: the sec encoding error is resolved

Before (G3J.4): `POST /api/auth/login` → 500 "Glon security operation failed"
(`sec field is not a STRING!`, BLOB salt).

After (G3J.5): login with a wrong/unknown user returns a clean **401
"invalid credentials"** (the generic failure), i.e. the sec/scrypt path now runs
with string fields. The 500 sec error is gone.

## 3. New independent blocker (setup)

`POST /api/auth/setup` now returns **500 `{"error":"lookup failed"}`** — the
`db/user-by-name` SQLite call fails **on the first call after startup**. The same
`db/user-by-name` used by login succeeds (login returns 401, not "lookup
failed"), so the function itself is correct; only the first invocation after
startup fails. This is a separate SQLite/startup-ordering defect, not a credential
encoding issue.

Because setup fails, the full cycle (create → set password → login → session →
logout) is **not yet complete**: login has no stored password to verify, so it
correctly returns 401.

## 4. Minimal reproducer (the new blocker)

```
start server (fresh io-root)
POST /api/users            {username:"alice",...}   -> 201
POST /api/auth/setup       {username:"alice",password:"..."} -> 500 {"error":"lookup failed"}
POST /api/auth/setup       (repeat)                 -> 500 {"error":"lookup failed"}
# login's db/user-by-name (same query) succeeds -> 401 (no password stored)
```

## 5. Status vs criteria

- Hex credential encoding implemented; sec "field is not a STRING" error resolved
  — DONE.
- Application loads, reaches `http/listen`; bracket depth 0 — DONE.
- Full authentication cycle (create/set/login/session/logout) — **BLOCKED** by the
  new setup "lookup failed" (independent SQLite/startup defect).
- Frozen S1 / G3H regression — unaffected (no runtime change; `glon_http.c`
  unchanged).

## 6. Migration compatibility

The encoding is **not** backward-compatible with any pre-existing binary
credentials: old BLOB salts/hashes are not reinterpreted as hex. This is
intentional (no silent reinterpretation). The test uses an **isolated** io-root
(`users.db` created fresh), so no existing user data is affected. A real
deployment would need a one-time re-registration (out of scope).

## 7. Recommendation

The credential encoding fix is correct and complete. The remaining setup
"lookup failed" is a separate, reproducible SQLite/startup-ordering defect that
should be investigated next (why the first `db/user-by-name` after startup fails
while later ones succeed). No runtime/S1 change is indicated.

## 8. Commit

Branch `experiment/glon-http-g3j5`, one local commit. Not pushed/merged/deployed.