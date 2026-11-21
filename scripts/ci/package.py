#!/usr/bin/env python3
"""Assemble a relocatable Glon distribution archive for one CI platform.

The archive contains the native executable, the Glon source vocabularies, the
HTTPS examples, documentation, dependency notes, the source commit and SHA-256
sums.  Optional shared libraries are copied into lib/; on macOS the workflow
rewrites install names before calling this script.
"""
import argparse
import hashlib
import shutil
import stat
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def copy_file(src, dst, mode=None):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    if mode is not None:
        dst.chmod(mode)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", required=True, choices=["linux", "macos", "windows", "android"])
    ap.add_argument("--arch", required=True)
    ap.add_argument("--variant", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--commit", required=True)
    ap.add_argument("--compiler", default="unknown")
    ap.add_argument("--glon", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--lib-dir", default="")
    ap.add_argument("--summary", default="")
    ap.add_argument("--archive-name", default="")
    ap.add_argument("--extra-file", action="append", default=[],
                    help="LABEL=PATH copied into the package root")
    ap.add_argument("--extra-dir", action="append", default=[],
                    help="LABEL=PATH copied recursively into the package root")
    opts = ap.parse_args()

    out = Path(opts.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    name = f"glon-{opts.version}-{opts.platform}-{opts.arch}-{opts.variant}"
    full = opts.variant in ("full", "https-large")
    file_cap = opts.variant == "full"
    windows = opts.platform == "windows"
    android = opts.platform == "android"
    glon = Path(opts.glon)
    if not glon.exists():
        sys.exit(f"missing executable {glon}")

    with tempfile.TemporaryDirectory(prefix="glon-package-") as td:
        stage = Path(td) / name
        stage.mkdir()

        copy_file(glon, stage / ("glon.exe" if windows else "glon"),
                  0o755 if not windows else None)
        for src in sorted((ROOT / "glon-lib").glob("*.glon")):
            copy_file(src, stage / "glon-lib" / src.name)
        # Never ship a package whose executable cannot find its mandatory
        # libraries just because a build staging directory was incomplete.
        lib_sources = {
            "prelude.glon": ROOT / "jupyter/prelude.glon",
            "strings.glon": ROOT / "demo/shop/strings.glon",
            "strings-ext.glon": ROOT / "demo/shop/strings-ext.glon",
            "file.glon": ROOT / "modules/file.glon",
            "file-stream.glon": ROOT / "modules/file-stream.glon",
            "http.glon": ROOT / "modules/http.glon",
        }
        for libname, src in lib_sources.items():
            dst = stage / "glon-lib" / libname
            if not dst.exists() and src.exists():
                copy_file(src, dst)
        for src in sorted((ROOT / "modules").glob("*.glon")):
            copy_file(src, stage / "modules" / src.name)

        exdir = stage / "examples"
        exdir.mkdir()
        for name4 in ("1-hello.glon", "2-static.glon", "3-routes.glon", "4-post-json.glon"):
            copy_file(ROOT / "demo/shop/https-examples" / name4, exdir / name4)
        copy_file(ROOT / "ci/conformance.glon", exdir / "conformance.glon")
        for d1 in ("hello-d1.glon", "json-smoke.glon", "file-smoke.glon"):
            copy_file(ROOT / "examples" / d1, exdir / d1)
        if file_cap:
            for src in sorted((ROOT / "examples/https-g3a").glob("*.glon")):
                copy_file(src, exdir / "https-g3a" / src.name)

        docs = {
            "AGENTS.md": ROOT / "AGENTS.md",
            "AI-QUICKSTART.md": ROOT / "AI-QUICKSTART.md",
            "VOCABULARY.json": ROOT / "VOCABULARY.json",
            "README.md": ROOT / "release/README.md",
            "RELEASE-MANIFEST.md": ROOT / "release/RELEASE-MANIFEST.md",
            "LICENSING.md": ROOT / "release/LICENSING.md",
            "THIRD-PARTY-NOTICES.md": ROOT / "release/THIRD-PARTY-NOTICES.md",
            "OPENSSL-APACHE-2.0.txt": ROOT / "release/OPENSSL-APACHE-2.0.txt",
            "MINGW-W64-RUNTIME.txt": ROOT / "ci/licenses/MINGW-W64-RUNTIME.txt",
            "GLON-WINDOWS-FILE-W5.md": ROOT / "docs/design/GLON-WINDOWS-FILE-W5.md",
            "GLON-DISTRIBUTIONS-D1.md": ROOT / "docs/design/GLON-DISTRIBUTIONS-D1.md",
            "D1-TESTING.md": ROOT / "docs/distribution/D1-TESTING.md",
        }
        if windows:
            docs["WINDOWS-QUICKSTART.md"] = ROOT / "WINDOWS-QUICKSTART.md"
        elif opts.platform == "macos":
            docs["MACOS-INTEL.md"] = ROOT / "docs/distribution/MACOS-INTEL.md"
        elif android:
            docs["ANDROID-TERMUX.md"] = ROOT / "docs/distribution/ANDROID-TERMUX.md"
        for dst, src in docs.items():
            copy_file(src, stage / dst)
        if windows:
            scripts = ["make-cert.ps1", "run-hello.ps1"]
        else:
            scripts = ["make-cert.sh", "run-hello.sh"]
        if file_cap:
            scripts.append("run-membership.sh")
        for name5 in scripts:
            copy_file(ROOT / "release/common" / name5, stage / name5,
                      0o755 if name5.endswith(".sh") else None)
        copy_file(ROOT / "scripts/dist" / ("d1-smoke.ps1" if windows else "d1-smoke.sh"),
                  stage / ("d1-smoke.ps1" if windows else "d1-smoke.sh"),
                  0o755 if not windows else None)

        if opts.lib_dir:
            libdir = Path(opts.lib_dir)
            if libdir.is_dir():
                # Windows resolves dependent DLLs from the executable's
                # directory (not a lib/ subdirectory), so place them beside
                # glon.exe.  macOS uses @executable_path/lib and keeps lib/.
                target = stage if windows else stage / "lib"
                for src in sorted(libdir.iterdir()):
                    if src.is_file():
                        copy_file(src, target / src.name)

        for spec in opts.extra_file:
            label, src = spec.split("=", 1)
            copy_file(Path(src), stage / label, 0o755)
        for spec in opts.extra_dir:
            label, src = spec.split("=", 1)
            srcd = Path(src)
            if srcd.is_dir():
                for pth in sorted(srcd.rglob("*")):
                    if pth.is_file():
                        rel = pth.relative_to(srcd).as_posix()
                        copy_file(pth, stage / label / rel,
                                 0o755 if pth.suffix == ".sh" else None)

        dep = ROOT / "ci/dependencies" / f"{opts.platform}.txt"
        dep_text = dep.read_text() if dep.exists() else "No dependency manifest supplied.\n"
        (stage / "DEPENDENCIES.txt").write_text(dep_text)
        (stage / "SOURCE-COMMIT").write_text(opts.commit + "\n")
        (stage / "BUILD-INFO.txt").write_text(
            f"platform={opts.platform}\narch={opts.arch}\nvariant={opts.variant}\n"
            f"version={opts.version}\nsource_commit={opts.commit}\ncompiler={opts.compiler}\n"
            f"file_module={'yes' if file_cap else 'no'}\n"
            f"https_module={'no' if android else 'yes'}\n")

        sums = []
        for p in sorted(stage.rglob("*")):
            if p.is_file() and p.name != "SHA256SUMS":
                rel = p.relative_to(stage).as_posix()
                h = hashlib.sha256(p.read_bytes()).hexdigest()
                sums.append(f"{h}  {rel}")
        (stage / "SHA256SUMS").write_text("\n".join(sums) + "\n")

        archive_name = opts.archive_name or (f"{name}.zip" if windows else f"{name}.tar.gz")
        if windows:
            archive = out / archive_name
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
                for p in sorted(stage.rglob("*")):
                    z.write(p, arcname=f"{name}/{p.relative_to(stage).as_posix()}")
        else:
            archive = out / archive_name
            with tarfile.open(archive, "w:gz") as t:
                t.add(stage, arcname=name)

        h = hashlib.sha256(archive.read_bytes()).hexdigest()
        print(f"ARCHIVE={archive}")
        print(f"SHA256={h}")
        print(f"BYTES={archive.stat().st_size}")
        if opts.summary:
            Path(opts.summary).write_text(
                f"| {opts.platform}-{opts.arch} | {opts.variant} | {archive.name} | "
                f"{archive.stat().st_size} | `{h}` |\n")

if __name__ == "__main__":
    main()
