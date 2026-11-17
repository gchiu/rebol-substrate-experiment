#!/usr/bin/env python3
"""site/check-site.py -- verify an assembled Pages tree before it is published.

  python3 site/check-site.py <docs-dir>

<docs-dir> is the directory GitHub Pages serves (docs/ on main, as assembled
by .github/workflows/deploy-pages.yml). The check fails (exit 1) unless:

  - docs/index.html is exactly site/index.html (the authoritative source);
  - the language reference and grammar assets are present;
  - the copied language Markdown matches the canonical repository documents;
  - every relative href/src in index.html, language/index.html and
    grammar/index.html resolves inside the tree (a link ending in "/" needs
    that directory's index.html);
  - every published diagram listed in grammar/railroad/manifest.json exists;
  - the published demos the landing page depends on are all present.

Absolute (https://) links are listed but not fetched: this check runs offline.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "site" / "index.html"
PAGES = ["index.html", "language/index.html", "grammar/index.html"]

LANGUAGE_COPIES = {
    "spec.md": "GLON-LANGUAGE-SPEC.md",
    "laws.md": "GLON-ALPHA-LAWS.md",
    "evaluation.md": "GLON-EVALUATION-MODEL.md",
    "host-abi.md": "GLON-HOST-ABI.md",
    "wasm-abi.md": "GLON-WASM-ABI.md",
    "verification.md": "GLON-SPEC-VERIFICATION.md",
    "implementer-guide.md": "GLON-IMPLEMENTER-GUIDE.md",
    "ai-authoring.md": "GLON-AI-AUTHORING.md",
    "syntax-diagrams.md": "GLON-SYNTAX-DIAGRAMS.md",
    "primer.md": "GLON-PRIMER.md",
    "GLON-ALPHA-LAWS.md": "GLON-ALPHA-LAWS.md",
}

# pages that must exist in every published tree (the landing page's promises)
REQUIRED = [
    "index.html",
    "r0-counter.html", "demo.js", "demo.wasm",
    "language/index.html", "language/spec.md", "language/laws.md",
    "language/evaluation.md", "language/host-abi.md", "language/wasm-abi.md",
    "language/verification.md", "language/implementer-guide.md",
    "language/ai-authoring.md", "language/syntax-diagrams.md", "language/primer.md",
    "language/GLON-ALPHA-LAWS.md",
    "grammar/index.html", "grammar/glon-grammar.ebnf", "grammar/railroad/manifest.json",
    "shop/index.html", "shop/glon.wasm",
    "shop/primer.html", "shop/primer-host.js",
    "shop/traffic.html", "shop/traffic-host.js",
    "shop/linda.html", "shop/linda-host.js",
    "shop/live.html", "shop/live-host.js", "shop/qwen-client.js",
    "shop/glon-live.wasm",
    "demos/patrol/patrol.html", "demos/patrol/patrol-host.js",
    "demos/patrol/patrol.glon", "demos/patrol/glon.wasm",
    "demos/patrol/prelude.glon", "demos/patrol/strings.glon",
    "demos/kaka/kaka.html", "demos/kaka/kaka-host.js",
    "demos/kaka/manifest.webmanifest",
    "demos/kaka/assets/icon-192.png", "demos/kaka/assets/icon-512.png",
    "demos/kaka/kaka.glon", "demos/kaka/kaka-lib.glon",
    "demos/kaka/kaka-draw.glon", "demos/kaka/kaka-rangi.glon", "demos/kaka/kaka-wave.glon", "demos/kaka/kaka-selftest.glon",
    "demos/kaka/glon.wasm", "demos/kaka/prelude.glon", "demos/kaka/strings.glon",
    "demos/kaka/assets/ChatGPT Image Oct 6, 2026, 08_56_38 AM-1.png",
    "demos/kaka/assets/ChatGPT Image Oct 6, 2026, 08_56_40 AM-2.png",
    "demos/kaka/assets/ChatGPT Image Oct 6, 2026, 08_56_41 AM-3.png",
    "demos/kaka/assets/ChatGPT Image Oct 6, 2026, 08_56_43 AM-4.png",
    "demos/kaka/assets/ChatGPT Image Oct 6, 2026, 12_37_49 PM-1.png",
    "demos/kaka/assets/ChatGPT Image Oct 6, 2026, 12_37_51 PM-2.png",
    "demos/kaka/assets/ChatGPT Image Oct 6, 2026, 12_37_53 PM-3.png",
    "demos/kaka/assets/ChatGPT Image Oct 6, 2026, 12_37_54 PM-4.png",
    "latex/index.html", "latex/latex-host.js", "latex/latex.css", "latex/latex.glon", "latex/infix.js",
    "latex/glon.wasm", "latex/prelude.glon", "latex/strings.glon",
    "latex/katex/katex.min.js", "latex/katex/katex.min.css", "latex/katex/LICENSE",
    "latex/katex/fonts/KaTeX_Main-Regular.woff2",
    "inflate/index.html", "inflate/app.js", "inflate/style.css", "inflate/glon.wasm",
    "inflate/prelude.glon", "inflate/strings.glon", "inflate/strings-ext.glon",
    "inflate/f2buf.glon", "inflate/inflate.glon", "inflate/inflate-driver.glon",
    "inflate/samples.json", "inflate/samples/hello.bin", "inflate/samples/longer.bin",
    "inflate/samples/streaming.bin", "inflate/samples/binary.bin",
    "inflate/samples/empty.bin", "inflate/samples/gzip.bin",
    "parse/index.html", "parse/app.js", "parse/style.css", "parse/glon.wasm",
    "parse/prelude.glon", "parse/strings.glon", "parse/strings-ext.glon",
    "parse/parse.glon", "parse/parse-driver.glon", "parse/examples.json",
    "parse/rules.js",
]


def page_links(docs, page, problems):
    path = docs / page
    if not path.is_file():
        return 0, 0
    html = path.read_text(encoding="utf-8")
    links = re.findall(r'(?:href|src)="([^"]+)"', html)
    external = 0
    relative = 0
    for link in links:
        if not link or link.startswith("#"):
            continue
        if re.match(r"^[a-z]+:", link) or link.startswith("//"):
            external += 1
            continue
        relative += 1
        clean = link.split("#", 1)[0]
        if not clean:
            continue
        target = (path.parent / clean).resolve()
        if clean.endswith("/"):
            target = target / "index.html"
        if not target.is_file():
            problems.append(f"broken link in {page}: {link}")
    return relative, external


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: check-site.py <docs-dir>")
    docs = pathlib.Path(sys.argv[1])
    problems = []

    index = docs / "index.html"
    if not index.is_file():
        sys.exit("check-site: FAIL: %s is missing" % index)
    if index.read_bytes() != SOURCE.read_bytes():
        problems.append("docs/index.html is not a verbatim copy of site/index.html")

    for rel in REQUIRED:
        if not (docs / rel).is_file():
            problems.append("missing published file: " + rel)

    for dest, source in LANGUAGE_COPIES.items():
        published = docs / "language" / dest
        canonical = ROOT / source
        if not published.is_file():
            problems.append("missing language document: language/" + dest)
        elif canonical.is_file() and published.read_bytes() != canonical.read_bytes():
            # The source branch carries the canonical root documents. A
            # production integration branch may intentionally publish only the
            # self-contained language/ copies, so compare only when the root
            # document exists.
            problems.append("language copy is stale: language/" + dest)

    manifest_path = docs / "grammar" / "railroad" / "manifest.json"
    if manifest_path.is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            problems.append("cannot parse grammar manifest: " + str(exc))
        else:
            for item in manifest.get("productions", []):
                rel = "grammar/railroad/" + item.get("svg", "")
                if not (docs / rel).is_file():
                    problems.append("missing grammar diagram: " + rel)
    else:
        problems.append("missing grammar/railroad/manifest.json")

    total_relative = 0
    total_external = 0
    for page in PAGES:
        rel, ext = page_links(docs, page, problems)
        total_relative += rel
        total_external += ext

    if problems:
        for p in problems:
            print("check-site: FAIL: " + p)
        sys.exit(1)
    print("check-site: OK: %d relative links resolve across %d pages, "
          "%d required files present, %d external links (not fetched)"
          % (total_relative, len(PAGES), len(REQUIRED), total_external))


if __name__ == "__main__":
    main()
