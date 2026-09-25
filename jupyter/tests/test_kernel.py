"""jupyter/tests/test_kernel.py -- the Glon kernel through the real Jupyter protocol.

Starts the kernel with jupyter_client.KernelManager from a kernelspec written
by glon_kernel.install into a temporary JUPYTER_PATH (nothing is installed for
the user), and talks to it over ZeroMQ exactly as JupyterLab / VS Code would.
No JupyterLab is needed.

Capacity coverage is split deliberately: jupyter/tests/test_host.py exercises
every resource edge exhaustively at the host level; here each resource
failure is checked once for its Jupyter mapping (GlonResourceError + detail)
and for the session surviving it.

Notebook validity (NB): frontends persist an output's message content into
the .ipynb as-is (JupyterLab's output model is {output_type, ...content}), so
every notebook-visible message must carry only nbformat-standard fields. The
outputs of every kind of cell below are assembled into a notebook exactly that
way and checked with nbformat.validate(); the host's structured outcome is
checked in the execute_reply's message metadata ("glon"), which is never saved.

Run with a Python that has ipykernel installed:
    python jupyter/tests/test_kernel.py        (or: make kernel-test PYTHON=...)
"""

import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))   # jupyter/

failures = 0


def check(cond, msg):
    global failures
    print(("  ok: " if cond else "  FAIL: ") + msg)
    if not cond:
        failures += 1


OUTPUT_TYPES = ("stream", "execute_result", "display_data", "error")


class Run:
    """Everything one execute_request produced."""

    def __init__(self, reply, iopub, meta=None, code=""):
        self.reply = reply
        self.meta = meta or {}
        self.code = code
        self.results = [m["content"] for m in iopub if m["msg_type"] == "execute_result"]
        self.errors = [m["content"] for m in iopub if m["msg_type"] == "error"]
        self.stdout = "".join(m["content"]["text"] for m in iopub
                              if m["msg_type"] == "stream" and m["content"]["name"] == "stdout")
        self.stderr = "".join(m["content"]["text"] for m in iopub
                              if m["msg_type"] == "stream" and m["content"]["name"] == "stderr")
        self.iopub = iopub

    @property
    def out(self):
        return self.results[0]["data"]["text/plain"] if len(self.results) == 1 else None

    @property
    def ename(self):
        return self.reply.get("ename")

    def outputs(self):
        """The notebook outputs a frontend would store: {output_type, **content}."""
        return [dict(m["content"], output_type=m["msg_type"]) for m in self.iopub
                if m["msg_type"] in OUTPUT_TYPES]


def iopub_for(kc, msg_id, timeout=120):
    msgs = []
    while True:
        m = kc.get_iopub_msg(timeout=timeout)
        if m["parent_header"].get("msg_id") != msg_id:
            continue
        if m["msg_type"] == "status" and m["content"]["execution_state"] == "idle":
            return msgs
        msgs.append(m)


def notebook_errors(runs):
    """Validate a notebook holding these cells' outputs; return error strings."""
    import nbformat
    nb = nbformat.v4.new_notebook()
    nb.metadata["kernelspec"] = {"name": "glon", "display_name": "Glon", "language": "glon"}
    problems = []
    try:
        for r in runs:
            nb.cells.append(nbformat.v4.new_code_cell(r.code, execution_count=r.reply.get("execution_count"),
                                                      outputs=[nbformat.from_dict(o) for o in r.outputs()]))
        nbformat.validate(nb)
        nbformat.validate(nbformat.reads(nbformat.writes(nb), as_version=4))  # saved + reopened
    except (nbformat.ValidationError, nbformat.validator.NotebookValidationError) as e:
        problems.append(str(e).split("\n")[0])
    return problems


def run(kc, code, silent=False, timeout=120):
    msg_id = kc.execute(code, silent=silent, store_history=not silent)
    while True:
        reply = kc.get_shell_msg(timeout=timeout)
        if reply["parent_header"].get("msg_id") == msg_id:
            break
    return Run(reply["content"], iopub_for(kc, msg_id, timeout), reply["metadata"], code)


def host_pids(kernel_pid):
    """PIDs of glon-kernel-host processes whose parent is the kernel (Linux /proc)."""
    pids = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % d) as f:
                stat = f.read()
        except OSError:
            continue
        comm = stat[stat.index("(") + 1:stat.rindex(")")]
        state, ppid = stat[stat.rindex(")") + 2:].split()[:2]
        if comm.startswith("glon-kernel-ho") and int(ppid) == kernel_pid and state != "Z":
            pids.append(int(d))
    return pids


def alive(pid):
    try:
        with open("/proc/%d/stat" % pid) as f:
            stat = f.read()
        return stat[stat.rindex(")") + 2] != "Z"
    except OSError:
        return False


def start(KernelManager):
    km = KernelManager(kernel_name="glon")
    km.start_kernel()
    kc = km.client()
    kc.start_channels()
    kc.wait_for_ready(timeout=120)
    return km, kc


def main():
    from glon_kernel import install
    from jupyter_client import KernelManager
    import ipykernel
    import jupyter_client
    import zmq

    print("Glon kernel: Jupyter protocol tests (python %s, ipykernel %s, jupyter_client %s, pyzmq %s)"
          % (sys.version.split()[0], ipykernel.__version__, jupyter_client.__version__, zmq.__version__))
    tmp = tempfile.mkdtemp(prefix="glon-kernelspec-")
    path = install.install(user=False, prefix=tmp)
    os.environ["JUPYTER_PATH"] = os.path.join(tmp, "share", "jupyter")
    check(os.path.isfile(os.path.join(path, "kernel.json")), "K0: the installer wrote a kernelspec (%s)" % path)

    km, kc = start(KernelManager)
    kpid = km.provisioner.process.pid
    nb_runs = {}   # kind -> Run, for the notebook-validity checks

    # ---- A: kernel_info ---------------------------------------------------------
    info = kc.kernel_info(reply=True, timeout=60)["content"]
    check(info["implementation"] == "glon" and info["language_info"]["name"] == "glon"
          and info["language_info"]["file_extension"] == ".glon",
          "A: kernel_info: implementation glon, language glon (.glon, %s)" % info["language_info"]["mimetype"])
    hosts = host_pids(kpid)
    check(len(hosts) == 1, "A2: the kernel owns exactly one native host process (pid %s)" % hosts)

    # ---- B-D: execution, persistence, closures -----------------------------------
    r = run(kc, "1")
    check(r.reply["status"] == "ok" and r.out == "1", "B: `1` -> execute_result 1")
    nb_runs["execute_result"] = r
    r1 = run(kc, "x: 21")
    r2 = run(kc, "* x 2")
    check(r1.out == "21" and r2.out == "42" and r2.reply["execution_count"] == r1.reply["execution_count"] + 1
          and r2.results[0]["execution_count"] == r2.reply["execution_count"],
          "C: In [%d]: x: 21  /  In [%d]: * x 2  ->  Out[%d]: 42"
          % (r1.reply["execution_count"], r2.reply["execution_count"], r2.reply["execution_count"]))
    run(kc, "counter: func [] [ n: 0  func [] [ n: + n 1 ] ]  c: counter")
    check(run(kc, "c").out == "1" and run(kc, "c").out == "2", "D: a closure keeps its state across cells (1, 2)")

    # ---- E: stdout --------------------------------------------------------------
    r = run(kc, "print 7")
    check(r.stdout == "7\n" and not r.results and r.reply["status"] == "ok",
          "E: `print 7` -> a stdout stream \"7\", and no Out[] (print returns no value)")
    nb_runs["stdout"] = r

    # ---- immutable strings across cells --------------------------------------------
    run(kc, 's: "hello "\nt: "world"')
    r = run(kc, "u: s/+ s t\nu")
    check(r.out == '"hello world"', 'ST1: cell `u: s/+ s t / u` -> Out "hello world"')
    check(run(kc, "s").out == '"hello "', 'ST2: a later cell `s` -> Out "hello " (s/+ did not change it)')
    r = run(kc, "s/print u")
    check(r.stdout == "hello world\n" and not r.results and r.reply["status"] == "ok",
          "ST3: `s/print u` -> stdout \"hello world\" (the bytes, not a number), no Out[]")

    # ---- F-G: SIN! --------------------------------------------------------------
    r = run(kc, "judge [ raise create-sin 'demo 'oops 7 ]")
    check(r.reply["status"] == "ok" and r.out == "#[SIN! demo oops 7]",
          "F: a judged SIN! is an ordinary execute_result")
    r = run(kc, "raise create-sin 'demo 'oops 7")
    check(r.reply["status"] == "error" and r.ename == "SIN!" and r.reply["evalue"] == "#[SIN! demo oops 7]"
          and r.meta["glon"]["sin"] == {"type": "demo", "id": "oops", "arg": "7"}
          and r.meta["glon"]["status"] == "UNCAUGHT_SIN"
          and len(r.errors) == 1 and r.errors[0]["ename"] == "SIN!",
          "G: an uncaught SIN! is a Jupyter error: ename SIN!, type demo / id oops / arg 7 "
          "(structured, in the reply metadata)")
    nb_runs["SIN! error"] = r
    check(run(kc, "* x 2").out == "42", "G2: the next cell still sees earlier state (42)")

    # ---- H: parse error -------------------------------------------------------------
    r = run(kc, "y: [ unclosed")
    check(r.ename == "GlonParseError" and r.reply["evalue"] == "syntax", "H: a malformed cell -> GlonParseError (syntax)")
    nb_runs["parse error"] = r
    check(run(kc, "* x 2").out == "42", "H2: earlier state remains (42)")

    # ---- K: generic halt ----------------------------------------------------------
    r = run(kc, "undefined-word")
    check(r.ename == "GlonHalt" and r.reply["evalue"] == "machine" and "[dump]" in r.stderr,
          "K: an unset word -> GlonHalt (machine), diagnostics on the stderr stream")
    nb_runs["GlonHalt"] = r
    check(km.is_alive() and run(kc, "x").out == "21", "K2: the kernel is alive and the session intact (x -> 21)")

    # ---- N, O, P, Q ---------------------------------------------------------------
    rs = [run(kc, src) for src in ("", "   \n\t  ", ";; only a comment")]
    check(all(r.reply["status"] == "ok" and not r.results and not r.errors for r in rs),
          "N: empty, whitespace-only and comment-only cells: ok, no Out[] (their value is none)")
    r = run(kc, "double: func [x] [\n    * x 2\n]\n;; comment line\ndouble 21\n")
    check(r.out == "42", "O: a multi-line cell with a comment runs as one cell (42)")
    count_before = run(kc, "0").reply["execution_count"]
    r = run(kc, "hidden: 5  print 99  hidden", silent=True)
    check(r.reply["status"] == "ok" and not r.results and not r.stdout and r.reply["execution_count"] == count_before,
          "P: silent=True executes but publishes no Out[] or stream, and does not advance the count")
    check(run(kc, "hidden").out == "5", "P2: the silent cell did run (hidden -> 5)")
    check(run(kc, "values [ 10 20 ]").out == "10 20" and run(kc, "1 2 3").out == "3",
          "Q: multiple results render space-separated (10 20); a cell's value is its last expression (3)")

    # ---- is_complete ------------------------------------------------------------------
    kc.is_complete("x: [")
    ic = kc.get_shell_msg(timeout=60)["content"]
    check(ic["status"] == "complete", "IC: is_complete answers `complete` (no parser in Python)")

    # ---- I, J: resource errors, one each ---------------------------------------------
    run(kc, "keep: 21")
    r = None
    for k in range(40):
        r = run(kc, " ".join("cg%d-%d: %d" % (k, j, j) for j in range(10)))
        if r.reply["status"] != "ok":
            break
    check(r.ename == "GlonResourceError" and r.reply["evalue"] == "context_full"
          and r.meta["glon"]["session"]["globals"] == 256,
          "I: filling the global context -> GlonResourceError (context_full) at 256/256")
    nb_runs["resource error"] = r
    check(run(kc, "keep").out == "21", "I2: earlier state remains (keep -> 21)")
    r = run(kc, " ".join("[ " + "[] " * 500 + "]" for _ in range(7)))
    check(r.ename == "GlonResourceError" and r.reply["evalue"] == "loader_exhausted",
          "J1: an oversized cell -> GlonResourceError (loader_exhausted)")
    r = run(kc, " ".join("[ " + "func [] 1 " * 140 + "]" for _ in range(5)))
    check(r.ename == "GlonResourceError" and r.reply["evalue"] == "site_table_full",
          "J2: 700 func sites -> GlonResourceError (site_table_full)")
    r = None
    for k in range(20):
        r = run(kc, "[ %s ] 0" % " ".join("sy%d-%d" % (k, j) for j in range(40)))
        if r.reply["status"] != "ok":
            break
    check(r.ename == "GlonResourceError" and r.reply["evalue"] == "symbol_table_full",
          "J3: too many distinct words -> GlonResourceError (symbol_table_full)")
    check(run(kc, "keep").out == "21" and km.is_alive(), "J4: after all of them the session still works")

    # ---- L: restart ----------------------------------------------------------------
    run(kc, "gone: 99")
    old_hosts = host_pids(kpid)
    km.restart_kernel(now=False)
    kc.wait_for_ready(timeout=120)
    kpid2 = km.provisioner.process.pid
    r = run(kc, "gone")
    check(r.ename == "GlonHalt", "L: after a restart the old definition is gone (GlonHalt: unset)")
    time.sleep(0.5)
    check(not any(alive(p) for p in old_hosts) and len(host_pids(kpid2)) == 1,
          "L2: the old host process is gone; the new kernel owns one new host")
    check(run(kc, 'str-eq "a" "a"').out == "1", "L3: the prelude is loaded in the new session")

    # ---- R: interrupt ---------------------------------------------------------------
    run(kc, "kept: 1")
    before = host_pids(kpid2)
    # seven nested loops of 10 (call depth ~70, under the ~100 return-stack
    # limit): about 10^7 calls, minutes of work -- far longer than the test waits
    loops = ["w7: func [n] [ either > n 0 [ w7 - n 1 ] [ 0 ] ]"]
    for k in range(6, 0, -1):
        loops.append("w%d: func [n] [ either > n 0 [ w%d 10  w%d - n 1 ] [ 0 ] ]" % (k, k + 1, k))
    r = run(kc, "  ".join(loops))
    check(r.reply["status"] == "ok", "R0: a long-running computation is defined")
    msg_id = kc.execute("w1 10")
    time.sleep(2)
    km.interrupt_kernel()
    reply = kc.get_shell_msg(timeout=120)
    while reply["parent_header"].get("msg_id") != msg_id:
        reply = kc.get_shell_msg(timeout=120)
    nb_runs["interrupted"] = Run(reply["content"], iopub_for(kc, msg_id), reply["metadata"], "w1 10")
    after = host_pids(kpid2)
    check(reply["content"]["status"] == "error" and reply["content"]["ename"] == "GlonInterrupted"
          and "state was lost" in reply["content"]["evalue"],
          "R1: interrupting a long cell -> GlonInterrupted, saying the session state was lost")
    check(km.is_alive() and km.provisioner.process.pid == kpid2,
          "R2: the Python kernel survived (same kernel process)")
    check(before and after and before != after and not any(alive(p) for p in before),
          "R3: only the host was replaced (host %s -> %s)" % (before, after))
    check(run(kc, "kept").ename == "GlonHalt" and run(kc, "+ 1 2").out == "3",
          "R4: the old Glon state is gone, and the new session works")

    # ---- SL: the host dies between cells -> GlonSessionLost ---------------------------
    run(kc, "kept2: 1")
    for p in host_pids(kpid2):
        os.kill(p, 9)
    time.sleep(0.5)
    r = run(kc, "kept2")
    check(r.ename == "GlonSessionLost" and "state was lost" in r.reply["evalue"]
          and r.meta["glon"]["status"] == "SESSION_LOST" and len(host_pids(kpid2)) == 1,
          "SL: a host killed between cells -> GlonSessionLost; a new host replaces it")
    check(run(kc, "kept2").ename == "GlonHalt" and run(kc, "+ 2 2").out == "4",
          "SL2: the replacement session is fresh and works")
    nb_runs["session lost"] = r

    # ---- NB: every notebook-visible output is valid nbformat --------------------------
    std = {"stream": {"name", "text"}, "execute_result": {"execution_count", "data", "metadata"},
           "error": {"ename", "evalue", "traceback"}}
    extra = sorted({"%s:%s" % (m["msg_type"], k) for r in nb_runs.values() for m in r.iopub
                    if m["msg_type"] in std for k in set(m["content"]) - std[m["msg_type"]]})
    check(not extra, "NB0: stream / execute_result / error messages carry only standard fields%s"
          % (" (extra: %s)" % extra if extra else ""))
    reply_std = {"status", "execution_count", "ename", "evalue", "traceback", "payload", "user_expressions"}
    bad = sorted({k for r in nb_runs.values() for k in set(r.reply) - reply_std})
    check(not bad, "NB1: execute_reply contents carry only standard fields%s" % (" (extra: %s)" % bad if bad else ""))
    kinds = ["execute_result", "stdout", "SIN! error", "parse error", "resource error", "GlonHalt",
             "interrupted", "session lost"]
    for kind in kinds:
        r = nb_runs.get(kind)
        probs = notebook_errors([r]) if r else ["not recorded"]
        check(r is not None and r.outputs() and not probs,
              "NB: a notebook holding a %s cell (%s) passes nbformat.validate()%s"
              % (kind, ", ".join(o["output_type"] for o in (r.outputs() if r else [])),
                 (": " + probs[0]) if probs else ""))
    probs = notebook_errors([nb_runs[k] for k in kinds if k in nb_runs])
    check(not probs, "NB-all: one notebook with all of them validates and survives save/reopen%s"
          % ((": " + probs[0]) if probs else ""))
    # the check is not vacuous: the old error content (with a "glon" field) is rejected
    old = Run({"status": "error", "execution_count": 1}, [{"msg_type": "error", "content": {
        "ename": "GlonHalt", "evalue": "machine", "traceback": [], "glon": {"status": "HALT"}}}], {}, "x")
    probs = notebook_errors([old])
    check(probs and "glon" in probs[0],
          "NB-neg: an error output with a custom `glon` field fails nbformat.validate() (%s)"
          % (probs[0] if probs else "accepted!"))

    # ---- M: two kernels -------------------------------------------------------------
    km2, kc2 = start(KernelManager)
    run(kc, "only-here: 1")
    check(run(kc2, "only-here").ename == "GlonHalt" and run(kc, "only-here").out == "1",
          "M: two kernels do not share state")

    # ---- S: shutdown leaves no host process -------------------------------------------
    hosts_1 = host_pids(kpid2)
    hosts_2 = host_pids(km2.provisioner.process.pid)
    for k, c in ((km, kc), (km2, kc2)):
        c.stop_channels()
        k.shutdown_kernel(now=False)
    time.sleep(1)
    check(hosts_1 and hosts_2 and not any(alive(p) for p in hosts_1 + hosts_2),
          "S: after shutdown no glon-kernel-host process remains (%s, %s)" % (hosts_1, hosts_2))

    # a kernel killed outright (no shutdown_request): its host sees EOF on its
    # request channel and exits by itself
    km3, kc3 = start(KernelManager)
    hosts_3 = host_pids(km3.provisioner.process.pid)
    kc3.stop_channels()
    km3.shutdown_kernel(now=True)
    deadline = time.time() + 10
    while time.time() < deadline and any(alive(p) for p in hosts_3):
        time.sleep(0.2)
    check(hosts_3 and not any(alive(p) for p in hosts_3),
          "S2: a hard-killed kernel leaves no orphan host either (%s)" % hosts_3)

    if failures == 0:
        print("all kernel protocol tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
