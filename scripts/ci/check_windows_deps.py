#!/usr/bin/env python3
"""Fail if a packaged Windows executable imports a non-system DLL that is not
present beside it.  Uses objdump's PE import table; no rename or PATH tricks.
"""
import argparse
import os
import subprocess
import sys

SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-win-")
SYSTEM = {
    "advapi32.dll", "bcrypt.dll", "crypt32.dll", "kernel32.dll", "msvcrt.dll",
    "ntdll.dll", "ole32.dll", "shell32.dll", "user32.dll", "ws2_32.dll",
    "wsock32.dll", "kernelbase.dll", "secur32.dll", "shlwapi.dll",
    "ucrtbase.dll", "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll",
}


def imports(objdump, path):
    result = subprocess.run([objdump, "-p", str(path)], check=False,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, errors="replace")
    names = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if line.lower().startswith("dll name:"):
            names.append(line.split(":", 1)[1].strip())
    if not names:
        raise RuntimeError(f"objdump could not read the PE import table: {path}\n{result.stderr[-300:]}")
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--objdump", default="objdump")
    opts = ap.parse_args()
    root = os.path.dirname(os.path.abspath(opts.exe))
    present = {n.lower() for n in os.listdir(root)}
    missing = []
    checked = set()

    def check(path):
        for dep in imports(opts.objdump, path):
            low = dep.lower()
            if low in checked:
                continue
            checked.add(low)
            if low in SYSTEM or low.startswith(SYSTEM_PREFIXES):
                continue
            if low not in present:
                missing.append(dep)

    check(opts.exe)
    if missing:
        print("MISSING_RUNTIME_DLLS: " + ", ".join(sorted(set(missing))))
        return 1
    print("WINDOWS_RUNTIME_DEPS_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
