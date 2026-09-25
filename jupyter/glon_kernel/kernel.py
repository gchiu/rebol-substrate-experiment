"""jupyter/glon_kernel/kernel.py -- the Glon Jupyter kernel.

A thin ipykernel wrapper around ONE native Glon session host
(jupyter/host/glon-kernel-host, driven through host.Host). The kernel owns the
host process: it starts it with the kernel, runs every cell in it, and
replaces it on restart or interrupt. There are no Glon semantics here: cells
are sent verbatim, and each outcome comes back from the host already
classified (status/detail/values/sin); this module only maps that structure to
Jupyter messages.

Rendering rules:
  - OK: the result values, molded by the runtime, joined with one space (the
    same rendering as the Saturnine notebook): `values [ 10 20 ]` shows
    `10 20`. No result (e.g. `print 7`) or a single `none` (e.g. an empty cell)
    shows no Out[] -- like Python's None.
  - stdout/stderr: the bytes the cell wrote, as `stream` messages.
  - UNCAUGHT_SIN -> error "SIN!"; PARSE_ERROR -> "GlonParseError";
    RESOURCE_ERROR -> "GlonResourceError"; HALT -> "GlonHalt". evalue carries
    the structured detail.
  - Notebook-visible outputs (stream, execute_result, error) carry ONLY the
    nbformat-standard fields, because frontends persist their content into the
    .ipynb: an extra field makes the saved notebook invalid. The host's
    structured outcome (status/detail/values/sin/session) is kept on the
    kernel (last_outcome) and exposed only in the execute_reply's message
    METADATA under "glon" -- standard Jupyter message metadata, which is never
    written into a notebook.
  - Interrupt: the host is killed and replaced (error "GlonInterrupted"); the
    Glon session state is lost. A host that dies mid-cell is replaced the same
    way (error "GlonSessionLost").
"""

import os
import subprocess

from ipykernel.kernelbase import Kernel

from .host import ROOT, Host, HostDied


def _project_version():
    """The repository commit this kernel runs from (the project has no release
    version yet)."""
    try:
        out = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, timeout=5)
        return out.stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


ENAMES = {
    "UNCAUGHT_SIN": "SIN!",
    "PARSE_ERROR": "GlonParseError",
    "RESOURCE_ERROR": "GlonResourceError",
    "HALT": "GlonHalt",
}

SESSION_LOST = "Glon session state was lost (a fresh session was started; all earlier definitions are gone)"


class GlonKernel(Kernel):
    implementation = "glon"
    implementation_version = _project_version()
    language = "glon"
    language_version = "alpha"
    language_info = {
        "name": "glon",
        "version": "alpha",
        "mimetype": "text/x-glon",
        "file_extension": ".glon",
    }
    banner = "Glon (alpha): a persistent Glon session in the native session host"

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.host = Host()
        # the structured outcome of the latest execute (never put in outputs)
        self.last_outcome = None

    # ---- host lifecycle -------------------------------------------------------
    def _replace_host(self):
        try:
            self.host.kill()
        except Exception:  # noqa: BLE001 -- the old host may already be gone
            pass
        self.host = Host()

    def _stream(self, name, data, silent):
        if data and not silent:
            self.send_response(self.iopub_socket, "stream",
                               {"name": name, "text": data.decode("utf-8", errors="replace")})

    def _error(self, ename, evalue, traceback, silent):
        # exactly the nbformat error-output fields (output_type is added by
        # the frontend from the message type)
        content = {"ename": ename, "evalue": evalue, "traceback": traceback}
        if not silent:
            self.send_response(self.iopub_socket, "error", content)
        return dict(content, status="error", execution_count=self.execution_count)

    def finish_metadata(self, parent, metadata, reply_content):
        # the structured outcome travels in the execute_reply's metadata only
        metadata = super().finish_metadata(parent, metadata, reply_content)
        if parent.get("header", {}).get("msg_type") == "execute_request" and self.last_outcome is not None:
            metadata["glon"] = self.last_outcome
        return metadata

    # ---- execute --------------------------------------------------------------
    async def do_execute(self, code, silent, store_history=True, user_expressions=None,
                         allow_stdin=False, *, cell_meta=None, cell_id=None):
        self.last_outcome = None
        try:
            res = self.host.execute(code)
        except KeyboardInterrupt:
            self.last_outcome = {"status": "INTERRUPTED"}
            self._replace_host()
            return self._error("GlonInterrupted", "interrupted: " + SESSION_LOST,
                               ["Interrupted: the Glon host was stopped and replaced.",
                                SESSION_LOST + "."], silent)
        except HostDied as e:
            self.last_outcome = {"status": "SESSION_LOST", "detail": str(e)}
            self._replace_host()
            return self._error("GlonSessionLost", "the Glon host died: " + SESSION_LOST,
                               ["The Glon host process ended unexpectedly (%s) and was replaced." % e,
                                SESSION_LOST + "."], silent)

        self._stream("stdout", res["stdout"], silent)
        self._stream("stderr", res["stderr"], silent)
        self.last_outcome = {k: res[k] for k in ("status", "detail", "values", "sin", "session")}
        status = res["status"]

        if status == "OK":
            values = res["values"]
            if not silent and values and values != ["none"]:
                self.send_response(self.iopub_socket, "execute_result", {
                    "execution_count": self.execution_count,
                    "data": {"text/plain": " ".join(values)},
                    "metadata": {},
                })
            return {"status": "ok", "execution_count": self.execution_count,
                    "payload": [], "user_expressions": {}}

        if status == "UNCAUGHT_SIN":
            s = res["sin"]
            molded = "#[SIN! %s %s %s]" % (s["type"], s["id"], s["arg"])
            return self._error("SIN!", molded,
                               ["** uncaught " + molded,
                                "type: %s  id: %s  arg: %s" % (s["type"], s["id"], s["arg"])],
                               silent)

        detail = res["detail"]
        explain = {
            "syntax": "the cell is not valid Glon source",
            "too_large": "the cell exceeds a size limit (a block or string over 512 elements, "
                         "or a cell over 16 KB)",
            "symbol_table_full": "the session's symbol table is full (512 distinct words)",
            "loader_exhausted": "the session's loader heap has no room for this cell",
            "site_table_full": "the session's function-site table is full (640 func literals)",
            "context_full": "context full: a context has no room for a new binding "
                            "(256 global, 16 per function)",
            "stack_sentry": "machine fail-stop: the stack sentry fired",
            "machine": "machine-level fail-stop (for example an unset word); not a SIN!",
        }.get(detail, detail)
        tail = ("The session is intact; restart the kernel for a fresh session if needed."
                if status == "RESOURCE_ERROR" else "Earlier definitions are still available.")
        return self._error(ENAMES.get(status, "GlonError"), detail,
                           ["%s: %s" % (ENAMES.get(status, "GlonError"), explain), tail],
                           silent)

    # ---- other requests ---------------------------------------------------------
    async def do_is_complete(self, code):
        # Glon exposes no structured parser completeness yet, and duplicating
        # the parser here is out of scope: a submitted cell is taken as complete
        # (a malformed one comes back as GlonParseError).
        return {"status": "complete"}

    async def do_shutdown(self, restart):
        try:
            self.host.quit()
        except Exception:  # noqa: BLE001 -- fall back to killing it
            try:
                self.host.kill()
            except Exception:  # noqa: BLE001
                pass
        return {"status": "ok", "restart": restart}
