#!/usr/bin/env python3
"""demo/shop/build-primer.py -- render the Glon primer from its one source.

The primer is the first Saturnine Glonbook: a notebook of runnable Glon
("Saturnine" is the notebook shell, "A Glonbook" its kind; the book's own
title comes from @title). The shell (page chrome, host, doc-tests) is not
specific to this book.

demo/shop/primer.txt holds the whole primer: prose, the runnable examples
(each with its expected outcome) and the Rebol/Red comparison table. This
script renders it twice:

  GLON-PRIMER.md        the portable, readable version (repository root)
  demo/shop/primer.html the live page: every example is an editable textarea
                        with Run/Reset, executed by primer-host.js through the
                        real WASM runtime (glon_run)

The expected outcomes are written into the Markdown only. The page carries no
results: it shows whatever the runtime returns. The native primer doc-tests
(r0_s1_primer_tests.c) and demo/shop/primer_node_test.js run every example
from primer.txt and compare with @expect.

  python3 demo/shop/build-primer.py          write both files
  python3 demo/shop/build-primer.py --check  exit 1 unless both are up to date

primer.txt format (line oriented):
  @title T / @intro / @section Heading    headings and prose (Markdown subset:
                                          paragraphs, "- " lists, `code`,
                                          **bold**, [text](url), <url>)
  @example id / [@env tasks] / @source / ...lines... / @expect / line / @end
                                          (in @expect, <site> stands for one
                                          positive integer: an internal lexical
                                          site id; everything else is exact)
  @table / | a | b | rows (first row is the header) / @end
"""

import html
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MANIFEST = HERE / "primer.txt"
MD_OUT = ROOT / "GLON-PRIMER.md"
HTML_OUT = HERE / "primer.html"
REPO_BLOB = "https://github.com/gchiu/rebol-substrate-experiment/blob/glon-shop-g1a/"
SHELL_NAME = "Saturnine"
SHELL_KIND = "A Glonbook"

# the per-Run environment, in load order (primer-host.js glon_loads these)
ENV = [("bootstrap", "bootstrap.glon"), ("strings", "strings.glon"), ("case", "case.glon"),
       ("tasks", "demos/tuple-space.glon")]


def parse(text):
    """-> (title, items); items: ('h', text) | ('p', lines) | ('ex', dict) | ('table', rows)"""
    title, items, prose = "", [], []
    lines = text.split("\n")
    i = 0

    def flush():
        if prose:
            items.append(("p", prose[:]))
            prose.clear()

    while i < len(lines):
        line = lines[i]
        if line.startswith("@title "):
            title = line[7:]
        elif line == "@intro":
            flush()
        elif line.startswith("@section "):
            flush()
            items.append(("h", line[9:]))
        elif line.startswith("@example "):
            flush()
            ex = {"id": line[9:], "env": "core", "source": [], "expect": None}
            i += 1
            mode = None
            while lines[i] != "@end":
                l = lines[i]
                if l == "@env tasks":
                    ex["env"] = "tasks"
                elif l == "@source":
                    mode = "s"
                elif l == "@expect":
                    mode = "e"
                elif mode == "s":
                    ex["source"].append(l)
                elif mode == "e" and ex["expect"] is None:
                    ex["expect"] = l
                i += 1
            while ex["source"] and not ex["source"][-1].strip():
                ex["source"].pop()
            items.append(("ex", ex))
        elif line == "@table":
            flush()
            rows = []
            i += 1
            while lines[i] != "@end":
                cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
                rows.append(cells)
                i += 1
            items.append(("table", rows))
        else:
            prose.append(line)
        i += 1
    flush()
    return title, items


def blocks(lines):
    """split prose lines into ('para', text) / ('list', [item text]) blocks"""
    out, para, items = [], [], None
    for l in lines + [""]:
        if l.startswith("- "):
            if para:
                out.append(("para", " ".join(para)))
                para = []
            if items is None:
                items = []
            items.append(l[2:].strip())
        elif l.startswith("  ") and items is not None and l.strip():
            items[-1] += " " + l.strip()
        elif not l.strip():
            if para:
                out.append(("para", " ".join(para)))
                para = []
            if items is not None:
                out.append(("list", items))
                items = None
        else:
            para.append(l.strip())
    return out


def inline_html(text):
    t = html.escape(text, quote=False)
    t = re.sub(r"`([^`]+)`", lambda m: "<code>" + m.group(1) + "</code>", t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", t)

    def link(m):
        label, url = m.group(1), m.group(2)
        if not re.match(r"https?://", url):
            url = REPO_BLOB + url
        return '<a href="' + url + '">' + label + "</a>"

    t = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, t)
    t = re.sub(r"&lt;(https?://[^&\s]+)&gt;", r'<a href="\1">\1</a>', t)
    return t


def render_md(title, items):
    out = ["<!-- Generated from demo/shop/primer.txt by demo/shop/build-primer.py."
           " Edit primer.txt, then rerun the script. -->", "", "# " + title, "",
           "*%s · %s*" % (SHELL_NAME, SHELL_KIND), ""]
    n = 0
    for kind, val in items:
        if kind == "h":
            n += 1
            out += ["## %d. %s" % (n, val), ""]
        elif kind == "p":
            for bk, bv in blocks(val):
                out += (["- " + x for x in bv] if bk == "list" else [bv]) + [""]
        elif kind == "ex":
            out += ["```"] + val["source"] + [";; => " + val["expect"], "```", ""]
        elif kind == "table":
            out.append("| " + " | ".join(val[0]) + " |")
            out.append("|" + "---|" * len(val[0]))
            out += ["| " + " | ".join(r) + " |" for r in val[1:]]
            out.append("")
    while out[-1] == "":
        out.pop()
    return "\n".join(out) + "\n"


def strip_comments(text):
    return "\n".join(line.split(";;", 1)[0].rstrip() for line in text.splitlines())


def render_html(title, items):
    body = []
    n = 0
    for kind, val in items:
        if kind == "h":
            n += 1
            body.append('<h2 id="s%d">%d. %s</h2>' % (n, n, inline_html(val)))
        elif kind == "p":
            for bk, bv in blocks(val):
                if bk == "list":
                    body.append("<ul>" + "".join("<li>" + inline_html(x) + "</li>" for x in bv) + "</ul>")
                else:
                    body.append("<p>" + inline_html(bv) + "</p>")
        elif kind == "ex":
            src = "\n".join(val["source"])
            rows = max(2, len(val["source"]))
            body.append(
                '<div class="ex" data-ex="%s" data-env="%s">\n'
                '<textarea rows="%d" spellcheck="false" autocapitalize="off" '
                'aria-label="Glon source: %s">%s</textarea>\n'
                '<div class="bar"><button class="run" disabled>Run</button>'
                '<button class="reset" disabled>Reset</button>'
                '<span class="hint">Ctrl+Enter runs</span></div>\n'
                '<pre class="out" aria-live="polite"></pre>\n</div>'
                % (val["id"], val["env"], rows, val["id"], html.escape(src, quote=False)))
        elif kind == "table":
            head = "".join("<th>" + inline_html(c) + "</th>" for c in val[0])
            trs = "".join("<tr>" + "".join("<td>" + inline_html(c) + "</td>" for c in r) + "</tr>"
                          for r in val[1:])
            body.append('<div class="tablewrap"><table><thead><tr>' + head +
                        "</tr></thead><tbody>" + trs + "</tbody></table></div>")

    env_blocks = "".join(
        '\n<script type="application/glon" data-env="%s">%s</script>'
        % (name, strip_comments((HERE / fname).read_text(encoding="utf-8")))
        for name, fname in ENV)

    return """<!doctype html>
<!-- Generated from demo/shop/primer.txt by demo/shop/build-primer.py. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%(title)s &middot; %(shell)s</title>
<style>
  :root { --fg: #16181d; --muted: #5d6470; --bg: #fdfdfc; --panel: #f3f4f6; --line: #d9dce1;
          --accent: #2456a6; --bad: #b3261e; --code: #eceef2; }
  @media (prefers-color-scheme: dark) {
    :root { --fg: #e6e8ec; --muted: #9aa1ad; --bg: #15171b; --panel: #1e2127; --line: #333842;
            --accent: #7fa7ec; --bad: #f2877e; --code: #262a31; }
  }
  * { box-sizing: border-box; }
  body { font-family: system-ui, sans-serif; line-height: 1.5; max-width: 820px; margin: 0 auto;
         padding: 1.5rem 16px 4rem; color: var(--fg); background: var(--bg); }
  h1 { font-size: 1.6rem; margin: 0.3rem 0 0.2rem; }
  .shell { display: flex; align-items: baseline; gap: 0.5rem; color: var(--muted); font-size: 0.9rem;
           border-bottom: 1px solid var(--line); padding-bottom: 0.4rem; }
  .shell strong { color: var(--fg); font-size: 1rem; letter-spacing: 0.02em; }
  h2 { font-size: 1.15rem; margin: 2.2rem 0 0.4rem; }
  a { color: var(--accent); }
  code, textarea, pre { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 0.92rem; }
  code { background: var(--code); padding: 0 0.25rem; border-radius: 3px; }
  #status { color: var(--muted); font-size: 0.9rem; }
  .ex { margin: 0.8rem 0 1.2rem; border: 1px solid var(--line); border-radius: 6px; background: var(--panel); }
  .ex textarea { display: block; width: 100%%; border: 0; border-bottom: 1px solid var(--line);
                 border-radius: 6px 6px 0 0; padding: 0.6rem 0.75rem; resize: vertical;
                 background: var(--bg); color: var(--fg); line-height: 1.45; }
  .bar { display: flex; gap: 0.5rem; align-items: center; padding: 0.4rem 0.6rem; }
  .bar button { font-size: 0.9rem; padding: 0.25rem 0.9rem; border-radius: 4px; border: 1px solid var(--line);
                background: var(--bg); color: var(--fg); cursor: pointer; }
  .bar button.run { background: var(--accent); border-color: var(--accent); color: #fff; }
  .bar button:disabled { opacity: 0.5; cursor: default; }
  .hint { margin-left: auto; color: var(--muted); font-size: 0.8rem; }
  .out { margin: 0; padding: 0 0.75rem; white-space: pre-wrap; overflow-wrap: anywhere; }
  .out:not(:empty) { padding: 0.5rem 0.75rem 0.6rem; border-top: 1px solid var(--line); }
  .out .res { font-weight: 600; }
  .out .sin { color: var(--bad); font-weight: 600; }
  .out .diag { color: var(--muted); font-size: 0.8rem; }
  .tablewrap { overflow-x: auto; }
  table { border-collapse: collapse; font-size: 0.9rem; width: 100%%; }
  th, td { border: 1px solid var(--line); padding: 0.35rem 0.5rem; text-align: left; vertical-align: top; }
  th { background: var(--panel); }
</style>
</head>
<body>
<header class="shell"><strong>%(shell)s</strong><span>%(kind)s</span></header>
<h1>%(title)s</h1>
<p id="status">Loading the Glon runtime&hellip;</p>
%(body)s
%(env)s
<script src="primer-host.js"></script>
</body>
</html>
""" % {"title": html.escape(title), "body": "\n".join(body), "env": env_blocks,
       "shell": SHELL_NAME, "kind": SHELL_KIND}


def main():
    check = "--check" in sys.argv[1:]
    title, items = parse(MANIFEST.read_text(encoding="utf-8"))
    outputs = [(MD_OUT, render_md(title, items)), (HTML_OUT, render_html(title, items))]
    stale = []
    for path, text in outputs:
        if check:
            current = path.read_bytes().decode("utf-8") if path.exists() else None
            if current != text:
                stale.append(path.name)
        else:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print("wrote %s" % path)
    if check:
        if stale:
            print("build-primer: STALE (rerun demo/shop/build-primer.py): " + ", ".join(stale))
            sys.exit(1)
        print("build-primer: GLON-PRIMER.md and demo/shop/primer.html are up to date "
              "(%d examples)" % sum(1 for k, _ in items if k == "ex"))


if __name__ == "__main__":
    main()
