# G3J.8 — Modularise User Lookup Using Existing Glon Libraries

Date: 2026-11-26
Branch: `experiment/glon-http-g3j8`
Baseline: G3J.7, commit `bf87d11`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Library + loader (verified)

- `tests/g3j/userlookup.glon` — a small library publishing `ul/user-by-name`
  (parameterised `SELECT … WHERE username = ?`), loaded **before** the app on the
  command line (the existing supported mechanism).
- Confirmed: library → app word sharing works. `tests/g3j/ul_test.glon` (a simple
  HTTP handler) calls `ul/user-by-name db "alice"` and returns **`LOOKUP-OK`**.

## 2. Connecting to auth/setup

Replaced `auth/setup`'s `db/user-by-name block-at result 0` with
`ul/user-by-name gw/users-db block-at result 0` (same library function, same
schema, no security change).

## 3. Result (one bounded test)

| Step | Result |
|---|---|
| create user | **201** (id 1) |
| setup password | **500 "internal error"** |
| login | 500 "internal error" |
| session | `{"user":null}` |
| logout | 411 (no body) |

The lookup **still fails inside `auth/setup`**, even via the library function.

## 4. Distinguishing library-context vs application-context

- **Library context (simple handler, `ul_test.glon`):** `ul/user-by-name` →
  `LOOKUP-OK` (success callback runs).
- **Application context (`auth/setup` nested callback):** the **same**
  `ul/user-by-name` → uncaught raise → `internal error`.

The library function and the SQLite query are correct (proven by `ul_test.glon`).
The failure is specific to **`auth/setup`'s nested callback context** (the deeply
nested `either sin? result [...] [...]` chain and its captured `request`/`result`
locals) — the same conclusion as G3J.6, now isolated with a library function.

## 5. Status vs criteria

- Small reusable user-lookup library — DONE (`userlookup.glon`).
- Deterministic test — DONE (`ul_test.glon` → LOOKUP-OK).
- Library lookup works in a handler — DONE.
- Connect to `auth/setup` / password setup succeeds — **NOT DONE** (500).
- One login — **NOT DONE** (setup fails first).
- Frozen S1 / loader / runtime / symbol capacity / schema — unchanged.

## 6. Blocker (smallest demonstration)

`ul/user-by-name` succeeds in a simple handler but raises inside `auth/setup`.
This is an **application-context failure**, not a library-context or SQLite
failure. Resolving it requires a focused investigation of the nested callback
context in `auth/setup` (a runtime/context matter), which is outside this
milestone's "no runtime change" scope.

## 7. Reproduce

```
make glon MODULES=file,http,sqlite,sec MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-bind 127.0.0.1 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write \
  modules/web.glon modules/json.glon tests/g3j/userlookup.glon tests/g3j/server-auth-attempt.glon
# POST /api/users -> 201; POST /api/auth/setup -> 500 internal error
# (contrast: tests/g3j/ul_test.glon -> LOOKUP-OK)
```

## 8. Commit

Branch `experiment/glon-http-g3j8`, one local commit. Not pushed/merged/deployed.