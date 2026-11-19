#!/usr/bin/env python3
"""Verify the generated Glon grammar reference.

Checks:
  - canonical grammar, syntax-doc EBNF and spec EBNF agree;
  - every production has a diagram and an HTML section;
  - generated files match a fresh in-memory generation byte for byte;
  - regeneration is deterministic;
  - HTML internal anchors and relative links resolve, with no network/CDN use;
  - every SVG is well-formed XML and has no external runtime references;
  - frozen S1 and implementation files are unchanged.

Exit status 0 means all checks pass. No files are written.
"""
from __future__ import annotations

import html.parser
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent

sys.path.insert(0, str(HERE))
import generate  # noqa: E402

errors: list[str] = []
checks = 0


def ok(message: str) -> None:
    global checks
    checks += 1
    print(f"  ok: {message}")


def fail(message: str) -> None:
    errors.append(message)
    print(f"  FAIL: {message}")


def check(condition: bool, message: str) -> None:
    if condition:
        ok(message)
    else:
        fail(message)


def extract_ebnf(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(r"```ebnf\n(.*?)```", text, re.S)
    if not match:
        raise SystemExit(f"no ebnf block in {path}")
    return match.group(1)


def compare_grammar(label: str, text: str, canonical: list[generate.Production]) -> None:
    try:
        other = generate.Parser(text).parse()
    except generate.GrammarError as exc:
        fail(f"{label}: cannot parse EBNF: {exc}")
        return
    a = [(p.name, re.sub(r"\s+", " ", p.rhs).strip()) for p in canonical]
    b = [(p.name, re.sub(r"\s+", " ", p.rhs).strip()) for p in other]
    if a == b:
        ok(f"{label}: {len(other)} productions match the canonical grammar")
    else:
        if [x[0] for x in a] != [x[0] for x in b]:
            fail(f"{label}: production names/order differ from canonical grammar")
        for (na, ra), (nb, rb) in zip(a, b):
            if na == nb and ra != rb:
                fail(f"{label}: RHS differs for {na}: {ra!r} != {rb!r}")
        if len(a) != len(b):
            fail(f"{label}: production count {len(b)} != canonical {len(a)}")


class LinkParser(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids: set[str] = set()
        self.hrefs: list[str] = []
        self.productions: list[str] = []
        self.raw_tags: list[str] = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        if "href" in attrs:
            self.hrefs.append(attrs["href"])
        if "data-production" in attrs:
            self.productions.append(attrs["data-production"])
        if tag in ("script", "link", "img", "iframe"):
            self.raw_tags.append(self.get_starttag_text() or tag)


def main() -> int:
    print("Glon grammar reference verification")

    # 1. Canonical source and published EBNF blocks.
    canonical = generate.parse_grammar()
    names = [p.name for p in canonical]
    check(len(names) == len(set(names)), "canonical grammar has unique production names")
    def source_doc(root_name: str, language_name: str) -> Path:
        root = REPO / root_name
        if root.is_file():
            return root
        return HERE.parent / "language" / language_name

    compare_grammar("syntax diagrams", extract_ebnf(source_doc("GLON-SYNTAX-DIAGRAMS.md", "syntax-diagrams.md")), canonical)
    compare_grammar("language specification", extract_ebnf(source_doc("GLON-LANGUAGE-SPEC.md", "spec.md")), canonical)

    # 2. Deterministic generation and expected files.
    prods1, out1 = generate.generate_outputs()
    prods2, out2 = generate.generate_outputs()
    check(out1 == out2, "in-memory generation is deterministic")
    check([p.name for p in prods1] == names, "generated production order matches canonical grammar")

    actual_svgs = sorted(p.name for p in generate.RAILROAD_DIR.glob("*.svg"))
    expected_svgs = sorted(name + ".svg" for name in names)
    check(actual_svgs == expected_svgs, f"one SVG per production ({len(expected_svgs)})")

    for rel, expected in out1.items():
        path = HERE / rel
        if not path.exists():
            fail(f"missing generated file: {rel}")
        elif path.read_bytes() != expected:
            fail(f"generated file differs from fresh generation: {rel}")
    if not errors:
        ok(f"all {len(out1)} generated files match deterministic output")

    manifest_path = generate.MANIFEST_PATH
    if manifest_path.exists():
        import json
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        check(manifest.get("production_count") == len(names), "manifest production count matches")
        check([p["name"] for p in manifest.get("productions", [])] == names, "manifest productions match canonical order")
        for item in manifest.get("productions", []):
            path = generate.RAILROAD_DIR / item["svg"]
            if not path.exists():
                fail(f"manifest references missing SVG: {item['svg']}")
                continue
            import hashlib
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != item["sha256"]:
                fail(f"manifest hash mismatch: {item['svg']}")
        if not any("manifest" in e for e in errors):
            ok("manifest hashes match the SVG files")
    else:
        fail("manifest is missing")

    # 3. HTML anchors, section/production coverage and relative links.
    index = generate.INDEX_PATH.read_text(encoding="utf-8")
    parser = LinkParser()
    parser.feed(index)
    check(set(names) == set(parser.productions), "every canonical production has an HTML data-production section")
    check(len(parser.productions) == len(names), "no duplicate HTML production sections")
    for name in names:
        if f"prod-{name}" not in parser.ids:
            fail(f"missing HTML anchor for production: {name}")
        if f"diagram-{name}" not in parser.ids:
            fail(f"missing diagram container for production: {name}")
    if not any("anchor" in e or "diagram container" in e for e in errors):
        ok("every production has a heading anchor and diagram container")

    broken = []
    for href in parser.hrefs:
        if href.startswith("#"):
            if href[1:] not in parser.ids:
                broken.append(href)
        elif href.startswith(("http://", "https://", "//")):
            broken.append(href)
        else:
            path = (HERE / href.split("#", 1)[0]).resolve()
            if not path.exists():
                broken.append(href)
    if broken:
        for item in sorted(set(broken)):
            fail(f"broken or external HTML link: {item}")
    else:
        ok(f"all {len(parser.hrefs)} HTML links are internal and resolve")

    external = [tag for tag in parser.raw_tags if re.search(r"(?:src|href)\s*=\s*['\"]?(?:https?:)?//", tag, re.I)]
    if external:
        for tag in external:
            fail(f"external runtime dependency in HTML: {tag}")
    else:
        ok("HTML has no external script/link/image/iframe dependency")

    for needle in ("UNRESOLVED", "Implementation-specific parsing behaviour", "Arity-driven expression grouping"):
        check(needle in index, f"HTML contains required section/text: {needle}")

    # 4. SVG well-formedness and no external runtime references.
    svg_bad = []
    for name in names:
        path = generate.RAILROAD_DIR / f"{name}.svg"
        try:
            ET.fromstring(path.read_text(encoding="utf-8"))
        except ET.ParseError as exc:
            svg_bad.append(f"{name}: {exc}")
            continue
        text = path.read_text(encoding="utf-8")
        for token in re.findall(r"https?://[^\s\"'<>]+", text):
            if token != "http://www.w3.org/2000/svg":
                svg_bad.append(f"{name}: external reference {token}")
        if f"<title>{name}</title>" not in text:
            svg_bad.append(f"{name}: missing title")
    if svg_bad:
        for item in svg_bad:
            fail(item)
    else:
        ok(f"all {len(names)} SVGs are well-formed and network-free")

    # 5. Frozen S1 and implementation files remain unchanged.
    try:
        result = subprocess.run(
            ["./check-frozen-s1.sh"],
            cwd=REPO,
            text=True,
            capture_output=True,
            check=False,
        )
        check(result.returncode == 0, "check-frozen-s1.sh passes")
        if result.returncode != 0:
            fail(result.stderr.strip() or result.stdout.strip())
    except OSError as exc:
        fail(f"cannot run check-frozen-s1.sh: {exc}")

    impl_paths = [
        "s1.c", "s1.h", "tests.c", "adversarial.c", "claims.c",
        "r0_s1_runtime.c", "r0_s1.h", "m1_layout.h",
    ]
    result = subprocess.run(
        ["git", "diff", "--quiet", "HEAD", "--", *impl_paths],
        cwd=REPO,
        check=False,
    )
    check(result.returncode == 0, "implementation and frozen S1 files are unchanged from HEAD")

    print()
    if errors:
        print(f"FAILED: {len(errors)} error(s), {checks} checks passed")
        return 1
    print(f"PASS: {checks} checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
