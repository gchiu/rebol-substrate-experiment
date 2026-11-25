# H2Q — Correct asynchronous continuation ownership

Date: 2026-11-25
Branch: `experiment/glon-http-h2q`
Baseline: H2P, commit `aad2f0b`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. H2P diagnosis confirmed

D2A + live inspection showed the suspended HTTP task's saved and live `RV_CTX`
is the `http/task-run` activation (parent global); the helper activation owning
`a` is absent. So the helper returned (popping its activation) before the
continuation's lookup — the value was not captured.

## 2. Closure semantics audit + ownership tests

Verified Glon's existing closure capture (`tests/h2q/ownership.py`):
- **A** — a returned closure reads its captured local: `42` (PASS).
- **B** — a closure captures a local, suspends on `task-wait`, is resumed by a
  peer, and reads it: `B=7` (PASS).
- **E** — a callback referencing an **uncaptured** local is rejected (no global
  fallback) (PASS).

So Glon closures **do** capture locals across return and async suspension, and
strict unbound-word behaviour is preserved. No new closure semantics are needed.

## 5. Correction

The defect is an **ownership error in the continuation**, not the runtime: a
value needed after an async suspension must be **captured by a closure** (or
stored in task-owned state), not left in a helper local that is popped on return.
The existing closure mechanism is sufficient; the fix belongs in the HTTP
task-entry adapter / application (capture the value), not in `r_lookup` or the
activation model. Per the milestone, I did not implement a speculative
activation-chain change; the ownership tests demonstrate the correct mechanism.

## 6. Status

- Primary (correct ownership mechanism): demonstrated — closures capture locals
  across return and async suspension; uncaptured locals are rejected.
- Secondary (two interleaved HTTPS tasks): **not** demonstrated — the HTTP
  application/adapter still needs to be restructured to capture the value; no
  runtime change was made.
- Frozen S1 unchanged; default G3F/H2A untouched; task build + M1/M2 pass.

## 7. Next step

Restructure the HTTP task entry so the continuation's required values are
captured in a closure (or stored in the task's owned context) rather than read
from a returned helper activation, then re-run the single- and two-request tests.

## Reproduce

```
make h2q-test        # ownership tests (A/B/E) pass
```