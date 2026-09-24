# Glon for Jupyter

A Jupyter kernel for Glon. Each kernel owns one native Glon **session host**
(`host/glon-kernel-host`): the Glon runtime is initialised once and every
cell runs in the same session, so definitions persist:

```
In [1]: x: 21
Out[1]: 21

In [2]: * x 2
Out[2]: 42
```

The Python layer is only glue. It sends each cell verbatim to the host and
maps the host's structured outcome to Jupyter messages; it has no Glon parser
or evaluator.

```
JupyterLab / Notebook / VS Code
        |  Jupyter protocol (ZeroMQ, provided by ipykernel)
glon_kernel.GlonKernel (ipykernel.kernelbase.Kernel subclass)
        |  framed requests / results + raw stdout, stderr
glon-kernel-host (one per kernel)
        |  r0_s1_session_run
the persistent Glon runtime (frozen S1 underneath)
```

## Setup (Linux / WSL)

Prerequisites: a C compiler and make, Python 3.10+, and a checkout of this
repository.

```bash
make glon-kernel-host                       # build the native session host
python3 -m venv ~/.venvs/glon-jupyter       # any environment you like
~/.venvs/glon-jupyter/bin/pip install ipykernel
make kernel-install PYTHON=~/.venvs/glon-jupyter/bin/python
```

(On Debian/Ubuntu without the `python3-venv` package, `python3 -m venv
--without-pip --system-site-packages DIR` followed by `DIR/bin/python -m pip
install ipykernel` gives an equivalent isolated environment without sudo.)

`make kernel-install` runs `python -m glon_kernel.install` from `jupyter/`.
It installs a per-user kernelspec named `glon` ("Glon") that launches that
Python with `-m glon_kernel`, and records the checkout's location
(`GLON_HOME`, `PYTHONPATH`); no paths are hard-coded. Use `--sys-prefix` to
install into the environment instead, or `--prefix DIR`. There is no wheel
yet: the kernel runs from the checkout.

Then choose the **Glon** kernel in JupyterLab or Notebook (installed
separately, e.g. `pip install jupyterlab`), or in VS Code's kernel picker
(VS Code reads the same Jupyter kernelspecs).

## Behaviour

| Glon outcome | Jupyter |
|---|---|
| values | `execute_result`, the runtime's rendering; several values (`values [ 10 20 ]`) are joined with one space: `10 20` |
| no value, or a single `none` (`print 7`, an empty cell) | no `Out[]` |
| `print` and `s/print` output | `stream` stdout |
| runtime diagnostics | `stream` stderr |
| a judged SIN! (`judge [ raise ... ]`) | an ordinary `execute_result`: `#[SIN! type id arg]` |
| an uncaught SIN! | `error` with ename `SIN!`; evalue `#[SIN! type id arg]` |
| malformed source | `error` `GlonParseError`: `syntax` or `too_large` |
| a full session capacity | `error` `GlonResourceError`: `symbol_table_full`, `loader_exhausted`, `site_table_full` or `context_full` |
| any other machine fail-stop | `error` `GlonHalt`: `machine` or `stack_sentry` |

Error replies also carry the host's structured outcome under `glon`. After
any of these errors the session keeps its earlier definitions (a failed parse
or a rejected binding changes nothing).

- **Restart** starts a fresh kernel and host: all definitions are gone.
- **Interrupt** stops the running cell by killing the host and starting a new
  one: the kernel survives, but **the Glon session state is lost** (error
  `GlonInterrupted`). State-preserving interrupts are future work.
- **Shutdown** quits the host; a host whose kernel dies exits when its request
  channel closes, so no host process is left behind.
- **is_complete** answers `complete`: Glon does not yet expose parser
  completeness, so incomplete-cell detection (for console frontends) is
  deferred. Notebook cells are unaffected.
- **silent** executions run but publish nothing and do not advance the count.

## Notebook environment

`prelude.glon` holds nine definitions copied verbatim from
`demo/shop/common.glon` (`mk-string`, `str-eq`, `get`, `lambda`, `does`,
`block-len`, `block-at`, `select`, `select-at`), and `demo/shop/case.glon`
adds CASE. The shop's web view dialect is not loaded, and tasks are not
included yet.

## Limits

The runtime's capacities are fixed for now. A fresh session uses:

| resource | capacity | used by a fresh session |
|---|---|---|
| distinct words (symbols) | 512 | 79 |
| global bindings | 256 | 37 |
| bindings per function | 16 | -- |
| func literals (sites) | 640 per session | -- |
| loader heap | ~14,000 cells | ~13,500 free |
| cell size | 16 KB, 512 values per block | -- |
| call depth | roughly 100 nested calls | -- |

Every parsed cell uses loader heap permanently, so a long session eventually
reports `GlonResourceError`; restart the kernel to continue. Output arrives
when a cell finishes (no live streaming yet).

**Platform:** proven on Linux/WSL. The host uses POSIX file descriptors and
`fopencookie` (glibc, musl); macOS and native Windows are not supported yet.

## The session host protocol

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
payload (binary-safe; no newline or NUL delimiters). Requests: `E` + cell
source, `R` (restart), `Q` (quit); EOF on stdin also quits. Results: one JSON
object per request, plus `{"op":"ready"}` at startup. An execute result
carries `status`, `detail`, `values`, `sin`, `stdout_bytes`, `stderr_bytes`
and `session`. The host counts the bytes each cell writes to stdout and stderr
and flushes them before the result, so output is attributed to its cell
exactly, with no markers.

## Tests

```bash
pip install -r jupyter/requirements-ci.txt                  # the pinned, tested versions
make host-test                                              # host only, stdlib Python
make kernel-test PYTHON=~/.venvs/glon-jupyter/bin/python    # the kernel through jupyter_client
make kernelspec-test PYTHON=~/.venvs/glon-jupyter/bin/python
```

`tests/test_host.py` covers the host exhaustively (every outcome and resource
edge). `tests/test_kernel.py` starts real kernels with
`jupyter_client.KernelManager` (no JupyterLab) from a kernelspec in a
temporary `JUPYTER_PATH`, and checks each Jupyter mapping, persistence,
restart, interrupt, isolation and shutdown. `tests/test_kernelspec.py`
installs the kernelspec into a temporary prefix with the real installer,
checks that `jupyter kernelspec list` finds it and that its `kernel.json` is
right, and runs one cell through it.

CI: `.github/workflows/jupyter.yml` runs all three from a clean Ubuntu runner
(Python 3.10, `requirements-ci.txt`), separately from the native/WASM/Pages
workflow.
