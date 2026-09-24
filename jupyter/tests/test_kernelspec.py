"""jupyter/tests/test_kernelspec.py -- install the Glon kernelspec and use it.

Proves the installation path a user takes, on a clean machine:
  1. `python -m glon_kernel.install --prefix TMP` (the real CLI, run from
     jupyter/ as `make kernel-install` does) writes the kernelspec;
  2. with JUPYTER_PATH pointing at TMP, `jupyter kernelspec list` discovers it;
  3. its kernel.json is right (name, language, launcher, checkout location, and
     no path that is not derived from this environment);
  4. that installed kernelspec launches a kernel that runs `* 7 6` -> 42.
Nothing is installed into the user's own Jupyter directories.

Run with a Python that has ipykernel installed:
    python jupyter/tests/test_kernelspec.py    (or: make kernelspec-test PYTHON=...)
"""

import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
JUPYTER_DIR = os.path.dirname(HERE)
ROOT = os.path.dirname(JUPYTER_DIR)

failures = 0


def check(cond, msg):
    global failures
    print(("  ok: " if cond else "  FAIL: ") + msg)
    if not cond:
        failures += 1


def under(path, base):
    path, base = os.path.realpath(path), os.path.realpath(base)
    return path == base or path.startswith(base + os.sep)


def main():
    print("Glon kernelspec: install, discover, launch")
    tmp = tempfile.mkdtemp(prefix="glon-kernelspec-prefix-")
    out = subprocess.run([sys.executable, "-m", "glon_kernel.install", "--prefix", tmp],
                         cwd=JUPYTER_DIR, capture_output=True, text=True)
    check(out.returncode == 0, "I1: `python -m glon_kernel.install --prefix TMP` succeeds (%s)"
          % (out.stdout.strip() or out.stderr.strip()))

    jupyter_path = os.path.join(tmp, "share", "jupyter")
    lst = subprocess.run([sys.executable, "-m", "jupyter", "kernelspec", "list", "--json"],
                         env=dict(os.environ, JUPYTER_PATH=jupyter_path), capture_output=True, text=True)
    specs = json.loads(lst.stdout)["kernelspecs"] if lst.returncode == 0 else {}
    check("glon" in specs and under(specs["glon"]["resource_dir"], tmp),
          "I2: `jupyter kernelspec list` discovers the glon kernelspec in the temporary prefix")

    kj = specs.get("glon", {}).get("spec", {})
    check(kj.get("display_name") == "Glon" and kj.get("language") == "glon",
          "I3: display_name Glon, language glon")
    argv = kj.get("argv", [])
    check(argv == [sys.executable, "-m", "glon_kernel", "-f", "{connection_file}"],
          "I4: argv runs this Python (%s) with -m glon_kernel -f {connection_file}" % sys.executable)
    envs = kj.get("env", {})
    check(under(envs.get("GLON_HOME", "/nonexistent"), ROOT) and under(ROOT, envs.get("GLON_HOME", "/nonexistent"))
          and under(envs.get("PYTHONPATH", "/nonexistent"), JUPYTER_DIR),
          "I5: GLON_HOME is this checkout (%s); PYTHONPATH is its jupyter/ directory" % envs.get("GLON_HOME"))
    text = json.dumps(kj)
    derived = argv[:1] == [sys.executable] and all(under(v, ROOT) for v in envs.values())
    check("\\\\" not in text and not re.search(r"\b[A-Za-z]:[\\/]", text) and derived,
          "I6: no Windows or machine-specific paths: every path is this Python or inside this checkout")

    # launch the INSTALLED kernelspec and run one cell
    os.environ["JUPYTER_PATH"] = jupyter_path
    from jupyter_client import KernelManager
    km = KernelManager(kernel_name="glon")
    km.start_kernel()
    kc = km.client()
    kc.start_channels()
    outputs = []
    try:
        kc.wait_for_ready(timeout=120)
        reply = kc.execute_interactive("* 7 6", timeout=120, output_hook=outputs.append)
        status = reply["content"]["status"]
    except Exception as e:  # noqa: BLE001 -- reported as a failed check
        status = "exception: %r" % e
    results = [m["content"]["data"]["text/plain"] for m in outputs if m["msg_type"] == "execute_result"]
    check(status == "ok" and results == ["42"],
          "I7: the installed kernelspec launches a Glon kernel: `* 7 6` -> 42")
    kc.stop_channels()
    km.shutdown_kernel(now=False)

    if failures == 0:
        print("all kernelspec tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
