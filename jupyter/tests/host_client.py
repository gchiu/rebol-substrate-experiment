"""jupyter/tests/host_client.py -- a stdlib-only client for glon-kernel-host.

Speaks the host's framed protocol (see jupyter/host/glon_kernel_host.c):
requests on the host's stdin, results on a dedicated pipe passed with
--result-fd, raw Glon output on stdout/stderr. It contains no Glon semantics:
it frames requests, decodes the JSON results the host sends, and attributes
output to a cell using the byte counts the host reports.

stdout and stderr are drained by background threads for the whole life of the
process, so a cell that prints a lot can never block the host on a full pipe.
"""

import json
import os
import struct
import subprocess
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
# GLON_KERNEL_HOST overrides the binary (e.g. a sanitizer build)
HOST = os.environ.get("GLON_KERNEL_HOST") or os.path.join(ROOT, "jupyter", "host", "glon-kernel-host")
DEFAULT_LIBS = [os.path.join(ROOT, "jupyter", "prelude.glon"),
                os.path.join(ROOT, "demo", "shop", "case.glon")]


class HostDied(Exception):
    """The host process ended (or closed its result channel) unexpectedly."""


class _Drain(threading.Thread):
    """Read one stream to EOF, keeping everything in a buffer."""

    def __init__(self, stream):
        super().__init__(daemon=True)
        self.stream = stream
        self.buf = bytearray()
        self.cond = threading.Condition()
        self.eof = False

    def run(self):
        while True:
            chunk = self.stream.read1(65536) if hasattr(self.stream, "read1") else self.stream.read(65536)
            with self.cond:
                if not chunk:
                    self.eof = True
                    self.cond.notify_all()
                    return
                self.buf += chunk
                self.cond.notify_all()

    def take(self, start, n, timeout):
        """Wait until bytes [start, start+n) have arrived and return them."""
        with self.cond:
            ok = self.cond.wait_for(lambda: len(self.buf) >= start + n or self.eof, timeout)
            if not ok or len(self.buf) < start + n:
                raise HostDied("expected %d output bytes, stream has %d" % (n, len(self.buf) - start))
            return bytes(self.buf[start:start + n])


class Host:
    def __init__(self, libs=None, exe=HOST, timeout=60):
        self.timeout = timeout
        rfd, wfd = os.pipe()
        args = [exe, "--result-fd", str(wfd)]
        for lib in (DEFAULT_LIBS if libs is None else libs):
            args += ["--lib", lib]
        # own session/process group, so an interrupt can later kill only the host
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, pass_fds=(wfd,),
                                     start_new_session=True)
        os.close(wfd)
        self.rfd = rfd
        self.out = _Drain(self.proc.stdout)
        self.err = _Drain(self.proc.stderr)
        self.out.start()
        self.err.start()
        self.out_pos = 0
        self.err_pos = 0
        self.ready = self._read_result()

    # ---- framing ------------------------------------------------------------
    def _read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = os.read(self.rfd, n - len(data))
            if not chunk:
                raise HostDied("result channel closed (host exit code %r)" % self.proc.poll())
            data += chunk
        return data

    def _read_result(self):
        (n,) = struct.unpack(">I", self._read_exact(4))
        return json.loads(self._read_exact(n).decode("utf-8"))

    def _send(self, payload):
        try:
            self.proc.stdin.write(struct.pack(">I", len(payload)) + payload)
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError) as e:
            raise HostDied("request channel closed: %s" % e)

    # ---- requests -----------------------------------------------------------
    def execute(self, source):
        """Run one cell; return the host's result plus this cell's stdout/stderr."""
        self._send(b"E" + source.encode("utf-8"))
        res = self._read_result()
        res["stdout"] = self.out.take(self.out_pos, res["stdout_bytes"], self.timeout)
        res["stderr"] = self.err.take(self.err_pos, res["stderr_bytes"], self.timeout)
        self.out_pos += res["stdout_bytes"]
        self.err_pos += res["stderr_bytes"]
        return res

    def restart(self):
        self._send(b"R")
        return self._read_result()

    def quit(self):
        self._send(b"Q")
        res = self._read_result()
        self.proc.wait(self.timeout)
        self.close()
        return res

    def kill(self):
        self.proc.kill()
        self.proc.wait(self.timeout)
        self.close()

    def close(self):
        if self.rfd is not None:
            os.close(self.rfd)
            self.rfd = None
        if self.proc.stdin and not self.proc.stdin.closed:
            try:
                self.proc.stdin.close()
            except OSError:
                pass
