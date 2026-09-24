"""python -m glon_kernel.install -- install the Glon kernelspec.

Runs from a repository checkout (no wheel yet). The kernelspec launches the
current Python interpreter with `-m glon_kernel`, and records where the checkout
is: GLON_HOME (the repository root, used to find the host binary and the
notebook libraries) and PYTHONPATH (so `glon_kernel` is importable).

  python -m glon_kernel.install              # per-user (default)
  python -m glon_kernel.install --sys-prefix # into the current environment
  python -m glon_kernel.install --prefix DIR # into DIR/share/jupyter/kernels
"""

import argparse
import json
import os
import sys
import tempfile

from jupyter_client.kernelspec import KernelSpecManager

from .host import HOST, ROOT

PACKAGE_PARENT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # jupyter/


def kernel_json():
    return {
        "argv": [sys.executable, "-m", "glon_kernel", "-f", "{connection_file}"],
        "display_name": "Glon",
        "language": "glon",
        "interrupt_mode": "signal",
        "env": {"GLON_HOME": ROOT, "PYTHONPATH": PACKAGE_PARENT},
        "metadata": {"debugger": False},
    }


def install(user=True, prefix=None, name="glon"):
    """Install the kernelspec; returns the directory it was installed to."""
    with tempfile.TemporaryDirectory() as td:
        with open(os.path.join(td, "kernel.json"), "w") as f:
            json.dump(kernel_json(), f, indent=2)
        return KernelSpecManager().install_kernel_spec(td, name, user=user, prefix=prefix)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Install the Glon Jupyter kernelspec.")
    where = ap.add_mutually_exclusive_group()
    where.add_argument("--user", action="store_true", help="install for this user (default)")
    where.add_argument("--sys-prefix", action="store_true", help="install into sys.prefix")
    where.add_argument("--prefix", help="install into PREFIX/share/jupyter/kernels")
    ap.add_argument("--name", default="glon", help="kernelspec name (default: glon)")
    args = ap.parse_args(argv)

    prefix = sys.prefix if args.sys_prefix else args.prefix
    path = install(user=prefix is None, prefix=prefix, name=args.name)
    print("Installed the Glon kernelspec in %s" % path)
    if not os.access(HOST, os.X_OK):
        print("warning: the session host %s is not built yet; run `make glon-kernel-host` in %s"
              % (HOST, ROOT))


if __name__ == "__main__":
    main()
