#!/usr/bin/env python3
"""experiments/glon-dom-host/run_browser.py -- run the host-backed DOM walk in a
real browser (Playwright's Chromium, headless) and check Glon's results.

It serves the repository root over HTTP on 127.0.0.1 (so every page is same
origin), opens index.html, and for each page:
  - asks Glon to extract its links (glonDomHost.extract: Glon walks the DOM);
  - computes a REFERENCE in JavaScript from a separate DOMParser parse:
    [a.getAttribute('href'), a.textContent] for every a[href], in document order
    (used only to check Glon; Glon never sees it);
  - prints Glon's links, the comparison, and the measurements.
Then it drives the page's own form for test-page.html and saves a screenshot.

  run_browser.py [--screenshot FILE] [URL ...]   (URLs relative to index.html)
Exit status 1 if any page's links differ from the reference.
"""
import functools, http.server, json, sys, threading, os
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

args = sys.argv[1:]
shot = None
if args[:1] == ["--screenshot"]:
    shot, args = args[1], args[2:]
urls = args or ["test-page.html", "build/pages/gen-100.html", "build/pages/gen-1000.html",
                "build/pages/gen-10000.html", "no-such-page.html", "https://example.com/"]

class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()
Quiet.extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm"}
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=ROOT))
threading.Thread(target=srv.serve_forever, daemon=True).start()
base = f"http://127.0.0.1:{srv.server_address[1]}/experiments/glon-dom-host/"

REFERENCE = """async (url) => {
  const html = await (await fetch(url)).text();
  const doc = new DOMParser().parseFromString(html, 'text/html');
  const nodes = doc.createTreeWalker(doc.documentElement, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
  let n = 1; while (nodes.nextNode()) n++;
  return { links: [...doc.querySelectorAll('a[href]')].map(a => [a.getAttribute('href'), a.textContent]),
           nodes: n };
}"""

failed = 0
with sync_playwright() as p:
    browser = p.chromium.launch()
    page = browser.new_page()
    errors = []
    page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
    page.goto(base + "index.html")
    page.evaluate("ready")
    print(f"browser: {browser.version}; served from {base}")
    for url in urls:
        heap0 = page.evaluate("performance.memory ? performance.memory.usedJSHeapSize : 0")
        try:
            r = page.evaluate("(u) => glonDomHost.extract(u)", url)
        except Exception as e:
            msg = str(e).splitlines()[0]
            print(f"\n== {url}\n   Glon reported an error: {msg[msg.find('Error'):][:160]}")
            continue
        heap1 = page.evaluate("performance.memory ? performance.memory.usedJSHeapSize : 0")
        ref = page.evaluate(REFERENCE, url)
        same = r["links"] == ref["links"]
        failed += not same
        m = r["metrics"]
        print(f"\n== {url}: {'MATCHES' if same else 'DIFFERS FROM'} the JavaScript reference "
              f"({len(r['links'])} links, {ref['nodes']} DOM nodes)")
        if len(r["links"]) <= 20:
            for i, (h, t) in enumerate(r["links"]):
                print(f"   {i + 1:2}. {h!r:58} {t!r}")
        if not same:
            print("   reference:", json.dumps(ref["links"])[:2000])
        ev = max(1, r["glonEvents"])
        print(f"   events {r['glonEvents']} (host sent {m['events']}), batches {m['batches']}, "
              f"bytes {m['bytes']}, dispatches {m['dispatches']}")
        print(f"   walk {m['wallMs']:.0f} ms: Glon {m['glonMs']:.0f} ms ({1000 * m['glonMs'] / ev:.0f} us/event), "
              f"host encoding {m['hostMs']:.1f} ms ({1000 * m['hostMs'] / max(1, m['batches']):.0f} us/batch)")
        print(f"   host: deepest cursor stack {m['maxStack']}, documents held after the walk {r['docsHeld']}, "
              f"JS heap {heap0 / 1e6:.1f} -> {heap1 / 1e6:.1f} MB")
    # the page's own form, for a screenshot
    page.fill("#url", "test-page.html")
    page.click("button")
    page.wait_for_function("document.querySelectorAll('#links tbody tr').length > 0")
    if shot:
        page.screenshot(path=shot, full_page=True)
        print(f"\nscreenshot: {shot}")
    if errors:
        print("\nconsole errors:", *errors, sep="\n   ")
    browser.close()
srv.shutdown()
sys.exit(1 if failed else 0)
