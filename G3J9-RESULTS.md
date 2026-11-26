# G3J.9 — Minimal Authentication Handler (runtime failure isolated)

Date: 2026-11-26
Branch: `experiment/glon-http-g3j9`
Baseline: G3J.8, commit `f391605`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Preserved

- `server-auth-attempt.glon` + G3J.6 diagnostics, `userlookup.glon`, `ul_test.glon`,
  `repro_lookup.glon` — untouched. `H3B-RESULTS.md` untracked/preserved.

## 2. Minimal app (`tests/g3j/min_auth.glon`)

A separate, flat Glon HTTP app (no deep `either sin? result` nesting):
- Seeds one user (id 1, "alice") + `user_passwords`/`sessions` tables at startup
  (migration split into 2-statement `exec`s to avoid the ≥3-statement defect).
- `POST /setup {username,password}` — `sec/random` salt → `sec/hex` → `sec/scrypt`
  → `sec/hex` → `sqlite` INSERT (hex credential storage).
- `POST /login {username,password}` — `sqlite` SELECT → `sec/scrypt` → `sec/hex` →
  `sec/ct-eq` → 200/401.
- Generic 401 for invalid/unknown (no enumeration).

## 3. Result (one bounded test)

| Request | Status | Body |
|---|---|---|
| `POST /setup` (alice/pw) | **500** | `no response` |
| `POST /login` (alice, correct) | 401 | `unauthorized` |
| `POST /login` (alice, wrong) | 401 | `unauthorized` |
| `POST /login` (ghost, unknown) | 401 | `unauthorized` |

- **User lookup works:** the login path runs the parameterised SELECT (wrong/unknown
  → 401, not a 500), so the DB + lookup are functional.
- **Password setup fails:** `POST /setup` returns **500 "no response"** — the async
  `sec/random → sec/scrypt → sqlite` chain does not complete within the request's
  pre-response pump, so no response is stored.
- Because setup never stores a hash, correct-password login is 401.

## 4. Blocker (smallest reproducer)

`POST /setup` on `tests/g3j/min_auth.glon` → `500 no response`. The handler queues
the async scrypt+sqlite chain and returns; the chain does not finish before the
host collects the response (the "no response" fallback fires). This is a runtime
async-completion issue in the non-task HTTP path, reproduced minimally (no deep
nesting, no JSON-heavy context).

Per the task, I stop at this reproducible runtime failure rather than begin a broad
runtime investigation.

## 5. Status vs criteria

- User lookup succeeds — DONE (login wrong/unknown → 401).
- Password setup succeeds — **NOT DONE** (500 "no response").
- Correct password accepted — **NOT DONE** (setup fails first).
- Incorrect/unknown → generic 401 — DONE.
- Server remains running — DONE (no halt).
- Frozen S1 / G3H regressions — unaffected (no runtime change).

## 6. Reproduce

```
make glon MODULES=file,http,sqlite,sec MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-bind 127.0.0.1 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write \
  modules/json.glon tests/g3j/min_auth.glon
# POST /setup -> 500 no response; POST /login -> 401
```

## 7. Commit

Branch `experiment/glon-http-g3j9`, one local commit. Not pushed/merged/deployed.