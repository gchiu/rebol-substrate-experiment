# Glon for Jupyter

Work in progress toward a Jupyter kernel for Glon. **Phase 1** (this
directory today) is the native session host; the ipykernel wrapper comes next.

## The session host

`host/glon_kernel_host.c` builds to `host/glon-kernel-host` (`make
glon-kernel-host`). One process is one Glon session: the runtime is
initialised once, the notebook libraries are loaded, and then cells run one
after another in the same runtime, so definitions persist:

```
cell 1:  x: 21        -> 21
cell 2:  * x 2        -> 42
```

The host adds no language semantics. Each outcome is classified from
structured runtime state by `r0_s1_session_run` (`../r0_s1_session.c`):

| status | detail | meaning |
|---|---|---|
| `OK` | `none` | ran cleanly; `values` holds the molded result(s) |
| `PARSE_ERROR` | `syntax`, `too_large` | the cell is not valid source |
| `RESOURCE_ERROR` | `symbol_table_full`, `loader_exhausted`, `site_table_full`, `context_full` | a fixed session capacity is exhausted |
| `UNCAUGHT_SIN` | `none` | a raised SIN! reached no `judge`; `sin` holds type/id/arg |
| `HALT` | `machine`, `stack_sentry` | another machine-level fail-stop |

A judged SIN! (`judge [ raise ... ]`) is an ordinary `OK` result. Parse and
resource failures are rolled back or detected before any write, so the session
keeps working after every one of these outcomes; `restart` gives a fresh
session.

### Protocol

```
glon-kernel-host --result-fd N [--lib FILE]...
```

| channel | direction | content |
|---|---|---|
| stdin | parent to host | framed requests |
| fd `N` (inherited, named on the command line) | host to parent | framed results |
| stdout | host to parent | raw Glon `print` output, never framed |
| stderr | host to parent | raw runtime diagnostics |

Framing, both directions: a 4-byte big-endian payload length, then the
payload (binary-safe; no newline or NUL delimiters).

- Requests: `E` + cell source (execute), `R` (restart), `Q` (quit). EOF on
  stdin also quits.
- Results: one JSON object per request, plus `{"op":"ready"}` at startup.
  An execute result carries `status`, `detail`, `values`, `sin`,
  `stdout_bytes`, `stderr_bytes` and `session` (symbols used, free loader
  cells, global bindings and capacity).

`stdout_bytes`/`stderr_bytes` are the exact number of bytes the cell wrote to
each stream (the host counts them), and everything is flushed before the
result is sent: a reader that has consumed that many bytes has all of the
cell's output. The parent must drain stdout and stderr concurrently.

### Notebook environment

`prelude.glon` holds nine definitions copied verbatim from
`demo/shop/common.glon` (`mk-string`, `str-eq`, `get`, `lambda`, `does`,
`block-len`, `block-at`, `select`, `select-at`), and `demo/shop/case.glon`
adds CASE. The shop's web view dialect is not loaded. A fresh session uses 81
of 512 symbols and 33 of 256 global bindings, with 13,494 loader cells free.
Tasks are not included yet.

### Tests

`make host-test` runs `tests/test_host.py`, which drives the host through
`tests/host_client.py` (Python standard library only, no Jupyter).

### Platform

Linux/WSL. The host uses POSIX file descriptors and `fopencookie` (glibc,
musl); macOS would need `funopen`, and native Windows a different way to pass
the result channel. Capacities (symbols, loader heap, contexts, func sites)
are the runtime's current fixed limits.
