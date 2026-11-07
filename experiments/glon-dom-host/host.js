/* experiments/glon-dom-host/host.js -- the browser host for the host-backed DOM
 * walk. The browser owns the DOM; Glon (in glon-live.wasm, the unchanged
 * GLON_LIVE build) walks it in batches and decides everything about it.
 *
 * This host does only mechanics:
 *   web-load url        fetch the page (same origin), parse it with DOMParser
 *                       (scripts do not run), keep it under a handle, and answer
 *                       web-loaded <handle> (or web-error <message>)
 *   dom-next "doc max close-depth names bytes"
 *                       encode the next events of that document's walk into one
 *                       batch (hostwalk.glon's format) and answer dom-events
 * It never decides which elements are links: it reports, for EVERY element, the
 * attributes Glon named. It releases a document once it has sent its end.
 *
 * Replies are delivered after Glon's dispatch has returned (a host_call happens
 * inside glon_event_bytes, which must not be re-entered), with queueMicrotask.
 */
(function () {
  "use strict";
  var ex = null;                       // the WASM exports
  var enc = new TextEncoder(), dec = new TextDecoder();
  var out = [];                        // bytes Glon printed (host_print)
  var docs = new Map(), nextHandle = 1;
  var waiter = null;                   // resolves when Glon prints its results
  var LIBS = [
    "../../jupyter/prelude.glon", "emit.glon", "../../demo/shop/strings.glon",
    "../../demo/shop/strings-ext.glon", "../glon-iter/iter.glon",
    "../glon-dom-links/list-block.glon", "../glon-dom-links/links.glon",
    "hostwalk.glon", "app.glon"
  ];

  // ---- metrics (a run's figures; reset by extract) ------------------------------
  var m;
  function resetMetrics() {
    m = { dispatches: 0, batches: 0, events: 0, bytes: 0, glonMs: 0, hostMs: 0,
          maxStack: 0, maxDocs: 0, t0: performance.now() };
  }
  resetMetrics();

  function view() { return new Uint8Array(ex.memory.buffer); }
  function alloc(bytes) {
    var p = ex.glon_alloc(bytes.length);
    if (!p) throw new Error("glon_alloc failed");
    view().set(bytes, p);
    return p;
  }

  // ---- calling Glon ---------------------------------------------------------------
  function dispatch(token, bytes) {
    var t = enc.encode(token);
    var tp = alloc(t), dp = alloc(bytes);
    var t0 = performance.now();
    var rc = ex.glon_event_bytes(tp, t.length, dp, bytes.length);
    m.glonMs += performance.now() - t0;
    m.dispatches++;
    if (rc !== 0) fail("glon_event_bytes('" + token + "') returned " + rc);
  }
  function deliver(token, bytes) {
    queueMicrotask(function () {
      try { dispatch(token, bytes); } catch (e) { fail(String(e)); }
    });
  }
  function fail(msg) {
    console.error("glon-dom-host: " + msg);
    if (waiter) { var w = waiter; waiter = null; w.reject(new Error(msg)); }
  }

  // ---- the walk of one document (host-held traversal state) ------------------------
  // A cursor is a stack of { node, next } frames over the RELEVANT children
  // (elements and text; comments, doctypes and processing instructions are not
  // reported). Text longer than a batch allows is split into several text events.
  function relevant(n) { return n.nodeType === 1 || n.nodeType === 3; }
  function nextRelevant(parent, i) {
    var kids = parent.childNodes;
    while (i < kids.length && !relevant(kids[i])) i++;
    return i;
  }
  function Walk(doc) {
    this.root = doc.documentElement;
    this.stack = [];
    this.started = false;
    this.ended = false;
    this.pending = [];                 // records not yet sent (split text, or one
  }                                    // that did not fit the last batch)

  function u16(a, v) { a.push((v >> 8) & 255, v & 255); }
  function pushBytes(a, b) { for (var i = 0; i < b.length; i++) a.push(b[i]); }

  Walk.prototype.visit = function (n, names, maxBytes) {
    var depth = this.stack.length, r;
    if (n.nodeType === 3) {
      var b = enc.encode(n.data), room = Math.max(1, maxBytes - 5), off = 0;
      do {                              // split without cutting a UTF-8 sequence
        var end = Math.min(b.length, off + room);
        while (end < b.length && end > off + 1 && (b[end] & 0xC0) === 0x80) end--;
        r = [4]; u16(r, depth); u16(r, end - off); pushBytes(r, b.subarray(off, end));
        this.pending.push(r);
        off = end;
      } while (off < b.length);
      return;
    }
    var has = nextRelevant(n, 0) < n.childNodes.length;
    var nm = enc.encode(n.localName).subarray(0, 255);
    r = [has ? 1 : 3]; u16(r, depth); r.push(nm.length); pushBytes(r, nm);
    for (var k = 0; k < names.length; k++) {
      var v = n.getAttribute(names[k]);
      if (v === null) { u16(r, 65535); continue; }
      var vb = enc.encode(v).subarray(0, 65534);
      u16(r, vb.length); pushBytes(r, vb);
    }
    this.pending.push(r);
    if (has) {
      this.stack.push({ node: n, next: 0 });
      if (this.stack.length > m.maxStack) m.maxStack = this.stack.length;
    }
  };
  // queue the next event's record(s)
  Walk.prototype.step = function (names, maxBytes) {
    if (!this.started) { this.started = true; this.visit(this.root, names, maxBytes); return; }
    if (this.stack.length === 0) { var e = [5]; u16(e, 0); this.pending.push(e); this.ended = true; return; }
    var top = this.stack[this.stack.length - 1];
    var i = nextRelevant(top.node, top.next);
    if (i < top.node.childNodes.length) {
      top.next = i + 1;
      this.visit(top.node.childNodes[i], names, maxBytes);
      return;
    }
    this.stack.pop();
    var c = [2]; u16(c, this.stack.length); this.pending.push(c);
  };
  // it/close at Glon: finish the open element at depth d (its children are skipped)
  Walk.prototype.close = function (d) {
    this.pending = [];
    while (this.stack.length - 1 > d) this.stack.pop();
    if (this.stack.length > 0) {
      var top = this.stack[this.stack.length - 1];
      top.next = top.node.childNodes.length;
    }
  };
  Walk.prototype.batch = function (max, names, maxBytes) {
    var bytes = [], count = 0;
    while (count < max) {
      if (this.pending.length === 0) {
        if (this.ended) break;
        this.step(names, maxBytes);
        continue;
      }
      var r = this.pending[0];
      if (count > 0 && bytes.length + r.length > maxBytes) break;   // at least one event
      this.pending.shift();
      pushBytes(bytes, r);
      count++;
    }
    return { bytes: Uint8Array.from(bytes), count: count };
  };

  // ---- Glon's requests ---------------------------------------------------------------
  function onHostCall(op, arg) {
    var t0 = performance.now();
    if (op === "web-load") {
      fetch(arg, { credentials: "same-origin" }).then(function (r) {
        if (!r.ok) throw new Error("HTTP " + r.status + " for " + arg);
        return r.text();
      }).then(function (html) {
        var doc = new DOMParser().parseFromString(html, "text/html");
        var h = nextHandle++;
        docs.clear();                     // one page at a time: a new load releases the old
        docs.set(h, { doc: doc, walk: new Walk(doc) });
        m.maxDocs = Math.max(m.maxDocs, docs.size);
        m.t0 = performance.now();          // time the walk, not the fetch:
        m.glonMs = 0; m.dispatches = 0;    // the counters start here too
        deliver("web-loaded", enc.encode(String(h)));
      }).catch(function (e) {
        deliver("web-error", enc.encode(String(e && e.message || e)));
      });
    } else if (op === "dom-next") {
      var f = arg.split(" ");
      var h = +f[0], max = +f[1], closeDepth = +f[2], names = f[3] ? f[3].split(",") : [], maxBytes = +f[4];
      var d = docs.get(h);
      if (!d) { deliver("web-error", enc.encode("no document " + h)); return; }
      if (closeDepth >= 0) d.walk.close(closeDepth);
      var b = d.walk.batch(max, names, maxBytes);
      if (d.walk.ended && d.walk.pending.length === 0) docs.delete(h);    // released after its end
      m.batches++; m.events += b.count; m.bytes += b.bytes.length;
      m.hostMs += performance.now() - t0;
      deliver("dom-events", b.bytes);
    } else {
      fail("unknown host_call op '" + op + "'");
    }
  }

  // ---- reading Glon's printed results -------------------------------------------------
  function outText() { return dec.decode(Uint8Array.from(out)); }
  function checkDone() {
    if (!waiter) return;
    var bytes = Uint8Array.from(out), s = dec.decode(bytes);
    var err = s.indexOf("ERROR ");
    if (err >= 0 && s.indexOf("\n", err) > 0) {
      var w0 = waiter; waiter = null; w0.reject(new Error(s.slice(err + 6, s.indexOf("\n", err)))); return;
    }
    if (!/EVENTS \d+ BATCHES \d+\n/.test(s)) return;
    // LINKS n, then 2n netstrings "len:bytes\n", over BYTES (lengths are byte counts)
    var i = indexOfBytes(bytes, enc.encode("LINKS "));
    var j = bytes.indexOf(10, i);
    var n = +dec.decode(bytes.subarray(i + 6, j)), p = j + 1, links = [];
    for (var k = 0; k < n; k++) {
      var pair = [];
      for (var q = 0; q < 2; q++) {
        var colon = bytes.indexOf(58, p);
        var len = +dec.decode(bytes.subarray(p, colon));
        pair.push(dec.decode(bytes.subarray(colon + 1, colon + 1 + len)));
        p = colon + 1 + len + 1;
      }
      links.push(pair);
    }
    var ev = /EVENTS (\d+) BATCHES (\d+)/.exec(s);
    var w = waiter; waiter = null;
    m.wallMs = performance.now() - m.t0;
    w.resolve({ links: links, glonEvents: +ev[1], glonBatches: +ev[2], metrics: m, docsHeld: docs.size });
  }
  function indexOfBytes(a, b) {
    outer: for (var i = 0; i + b.length <= a.length; i++) {
      for (var k = 0; k < b.length; k++) if (a[i + k] !== b[k]) continue outer;
      return i;
    }
    return -1;
  }

  // ---- loading ---------------------------------------------------------------------------
  function imports() {
    return { env: {
      host_print: function (ptr, len) {
        var b = view().subarray(ptr, ptr + len);
        for (var i = 0; i < len; i++) out.push(b[i]);
        if (b[len - 1] === 10) checkDone();
      },
      host_set_text: function () {},
      host_set_html: function (handle, ptr, len) {
        var el = document.getElementById("glon-view");
        if (el) el.innerHTML = dec.decode(view().slice(ptr, ptr + len));   // Glon's fixed-text summary
      },
      host_canvas_script: function () {},
      host_call: function (opPtr, opLen, argPtr, argLen) {
        var op = dec.decode(view().slice(opPtr, opPtr + opLen));
        var arg = dec.decode(view().slice(argPtr, argPtr + argLen));
        try { onHostCall(op, arg); } catch (e) { fail(String(e)); }
      }
    } };
  }
  // the CLI's preparation of a source: strip ;; comments, wrap in [ ] if needed
  function prepare(src) {
    src = src.split("\n").map(function (l) { var c = l.indexOf(";;"); return c >= 0 ? l.slice(0, c) : l; }).join("\n");
    return /^\s*\[/.test(src) ? src : "[ " + src + " ]";
  }
  function boot(wasmUrl) {
    return fetch(wasmUrl).then(function (r) { return r.arrayBuffer(); })
      .then(function (b) { return WebAssembly.instantiate(b, imports()); })
      .then(function (res) {
        ex = res.instance.exports;
        if (ex.glon_init() !== 0) throw new Error("glon_init failed");
        return LIBS.reduce(function (p, path) {
          return p.then(function () { return fetch(path); })
            .then(function (r) { if (!r.ok) throw new Error(path + ": HTTP " + r.status); return r.text(); })
            .then(function (src) {
              var b = enc.encode(prepare(src));
              var rc = ex.glon_load(alloc(b), b.length);
              if (rc !== 0) throw new Error("glon_load(" + path + ") = " + rc);
            });
        }, Promise.resolve());
      });
  }

  // extract(url): Glon walks the page at url; resolves with the links Glon found
  function extract(url) {
    if (waiter) return Promise.reject(new Error("an extraction is already running"));
    out = []; resetMetrics();
    return new Promise(function (resolve, reject) {
      waiter = { resolve: resolve, reject: reject };
      deliver("web-load", enc.encode(url));
    });
  }

  window.glonDomHost = { boot: boot, extract: extract, outText: outText,
                         docsHeld: function () { return docs.size; } };
})();
