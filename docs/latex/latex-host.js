/* experiments/glon-latex/web/latex-host.js -- browser host for the Glon LaTeX page.
 *
 * The same pattern as the Saturnine primer (demo/shop/primer-host.js) and
 * Glon Patrol (demos/patrol/patrol-host.js):
 *
 *   - glon.wasm (the unchanged shop runtime) is compiled ONCE;
 *   - every Run instantiates it afresh, so each example starts from a clean
 *     machine: glon_init, then glon_load of prelude.glon, strings.glon and
 *     latex.glon (fetched once, as Patrol fetches its libraries), then
 *     glon_run of the example's CURRENT text;
 *   - what Glon printed (host_print) is shown line by line. A printed line
 *     of the form \[...\] or $...$ is ALSO typeset with the local KaTeX, and
 *     the LaTeX itself is shown beneath it.
 *
 * This file evaluates no Glon and builds no LaTeX: the l/ vocabulary in
 * latex.glon produces every string. KaTeX is presentation only.
 */
(function () {
  "use strict";

  var LIBS = ["prelude.glon", "strings.glon", "latex.glon"];
  var dec = new TextDecoder();
  var enc = new TextEncoder();
  var statusEl = document.getElementById("status");

  function fetchText(url) {
    return fetch(url).then(function (r) {
      if (!r.ok) throw new Error(url + ": HTTP " + r.status);
      return r.text();
    });
  }
  function fetchBytes(url) {
    return fetch(url).then(function (r) {
      if (!r.ok) throw new Error(url + ": HTTP " + r.status);
      return r.arrayBuffer();
    });
  }

  var modulePromise = (typeof WebAssembly.compileStreaming === "function"
    ? WebAssembly.compileStreaming(fetch("glon.wasm")).catch(function () {
        return fetchBytes("glon.wasm").then(function (b) { return WebAssembly.compile(b); });
      })
    : fetchBytes("glon.wasm").then(function (b) { return WebAssembly.compile(b); }));
  var libsPromise = Promise.all(LIBS.map(fetchText));

  function linesOf(bytes) {
    var lines = dec.decode(new Uint8Array(bytes)).split("\n");
    if (lines.length && lines[lines.length - 1] === "") lines.pop();
    return lines;
  }

  // One fresh runtime per run; returns { printed: [line], result, error }.
  function runProgram(source) {
    return Promise.all([modulePromise, libsPromise]).then(function (v) {
      var mod = v[0], libs = v[1];
      var ex = null;
      var out = [];
      function view() { return new Uint8Array(ex.memory.buffer); }
      var imports = {
        env: {
          host_print: function (ptr, len) {
            var b = view().subarray(ptr, ptr + len);
            for (var k = 0; k < b.length; k++) out.push(b[k]);
          },
          // a printed integer >= 1000000 arrives here (handle * 1000000 + value)
          host_set_text: function (handle, value) {
            enc.encode(String(handle * 1000000 + value) + "\n").forEach(function (c) { out.push(c); });
          },
          host_set_html: function () {},
          host_canvas_script: function () {}
        }
      };
      return WebAssembly.instantiate(mod, imports).then(function (instance) {
        ex = instance.exports;
        function put(str) {
          var bytes = enc.encode(str);
          var p = ex.glon_alloc(bytes.length);
          view().set(bytes, p);
          return [p, bytes.length];
        }
        if (ex.glon_init() !== 0) return { printed: [], error: "glon_init failed" };
        for (var i = 0; i < libs.length; i++) {
          var a = put(libs[i]);
          var rc = ex.glon_load(a[0], a[1]);
          if (rc !== 0) return { printed: linesOf(out), error: LIBS[i] + " failed to load (rc " + rc + ")" };
        }
        var before = out.length;
        var s = put(source);
        if (ex.glon_run(s[0], s[1]) !== 0) return { printed: linesOf(out.slice(before)), error: "program too long" };
        var ptr = ex.glon_result_ptr();
        var len = ex.glon_result_len();
        return { printed: linesOf(out.slice(before)), result: dec.decode(view().subarray(ptr, ptr + len)) };
      });
    });
  }

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  // A printed line that is display ( \[...\] ) or inline ( $...$ ) math.
  function mathOf(line) {
    var m = /^\\\[([\s\S]*)\\\]$/.exec(line);
    if (m) return { tex: m[1], display: true };
    m = /^\$([\s\S]+)\$$/.exec(line);
    if (m) return { tex: m[1], display: false };
    return null;
  }

  function show(outEl, r) {
    outEl.textContent = "";
    r.printed.forEach(function (line) {
      var m = mathOf(line);
      if (!m) {
        // "[dump] ..." is the runtime's own diagnostic line on a halt
        outEl.appendChild(el("div", line.indexOf("[dump]") === 0 ? "line diag" : "line", line));
        return;
      }
      var box = el("div", "math" + (m.display ? " display" : " inline"));
      try {
        katex.render(m.tex, box, { displayMode: m.display, throwOnError: true });
      } catch (e) {
        box.className = "math tex-error";
        var msg = String(e && e.message || e);
        box.textContent = /^KaTeX/.test(msg) ? msg : "KaTeX: " + msg;
      }
      outEl.appendChild(box);
      outEl.appendChild(el("code", "tex", line));
    });
    if (r.error) { outEl.appendChild(el("div", "line sin", "host error: " + r.error)); return; }
    if (r.result.indexOf("**") === 0) outEl.appendChild(el("div", "line sin", r.result));
    else if (r.result !== "none") outEl.appendChild(el("div", "line res", "=> " + r.result));
    if (!r.printed.length && r.result === "none") outEl.appendChild(el("div", "line res", "=> none (nothing printed)"));
  }

  var pending = 0;
  function busy(delta) {
    pending += delta;
    document.body.setAttribute("data-busy", String(pending));
  }

  function wire(box) {
    var ta = box.querySelector("textarea");
    var run = box.querySelector("button.run");
    var reset = box.querySelector("button.reset");
    var out = box.querySelector(".out");
    function go() {
      if (run.disabled) return Promise.resolve();
      run.disabled = true;
      busy(1);
      out.textContent = "running…";
      return runProgram(ta.value).then(function (r) { show(out, r); })
        .catch(function (e) { out.textContent = ""; out.appendChild(el("div", "line sin", "host error: " + (e && e.message || e))); })
        .then(function () { run.disabled = false; busy(-1); });
    }
    run.addEventListener("click", go);
    if (reset) reset.addEventListener("click", function () { ta.value = ta.defaultValue; go(); });
    ta.addEventListener("keydown", function (e) {
      if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); go(); }
    });
    run.disabled = false;
    if (reset) reset.disabled = false;
    return go;
  }

  // The Infix input section (v003). GlonInfix (infix.js) is the FRONT END: it
  // rewrites infix text into Glon prefix source. That source then runs exactly
  // like any other example; nothing here evaluates or typesets mathematics.
  function wireInfix(box) {
    var ta = box.querySelector("textarea");
    var run = box.querySelector("button.run");
    var reset = box.querySelector("button.reset");
    var glonOut = box.querySelector(".glon-out");
    var out = box.querySelector(".out");
    function go() {
      if (run.disabled) return Promise.resolve();
      out.textContent = "";
      glonOut.textContent = "";
      glonOut.className = "glon-out";
      var t = GlonInfix.translate(ta.value);
      if (t.error) {
        // the input line with a caret under the reported position
        var lines = ta.value.split("\n"), at = t.pos, row = 0;
        while (row < lines.length - 1 && at > lines[row].length) { at -= lines[row].length + 1; row++; }
        glonOut.className = "glon-out sin";
        glonOut.textContent = "infix error: " + t.error + "\n" + (lines[row] || "") + "\n" + new Array(at + 1).join(" ") + "^";
        return Promise.resolve();
      }
      var source = GlonInfix.program(t.glon);
      glonOut.textContent = source;
      run.disabled = true;
      busy(1);
      out.textContent = "running…";
      return runProgram(source).then(function (r) { show(out, r); })
        .catch(function (e) { out.textContent = ""; out.appendChild(el("div", "line sin", "host error: " + (e && e.message || e))); })
        .then(function () { run.disabled = false; busy(-1); });
    }
    run.addEventListener("click", go);
    reset.addEventListener("click", function () { ta.value = ta.defaultValue; go(); });
    ta.addEventListener("keydown", function (e) {
      if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); go(); }
    });
    Array.prototype.forEach.call(document.querySelectorAll(".chip[data-infix]"), function (chip) {
      chip.addEventListener("click", function () { ta.value = chip.getAttribute("data-infix"); go(); });
    });
    run.disabled = false;
    reset.disabled = false;
    return go;
  }

  if (typeof katex === "undefined") {
    statusEl.textContent = "Could not load the local KaTeX (katex/katex.min.js).";
    return;
  }

  Promise.all([modulePromise, libsPromise]).then(function (v) {
    var names = WebAssembly.Module.exports(v[0]).map(function (e) { return e.name; });
    if (names.indexOf("glon_run") < 0) throw new Error("this glon.wasm predates glon_run");
    var runs = Array.prototype.map.call(document.querySelectorAll(".ex"), wire);
    var infixBox = document.getElementById("infix");
    if (infixBox && typeof GlonInfix !== "undefined") runs.push(wireInfix(infixBox));
    statusEl.textContent = "Running every example in the Glon WebAssembly runtime…";
    // Run every example once, in order, so the page opens with all figures drawn.
    return runs.reduce(function (p, go) { return p.then(go); }, Promise.resolve()).then(function () {
      statusEl.textContent = "Ready. Every example is editable: change the Glon and press Run (or Ctrl+Enter).";
      document.body.setAttribute("data-ready", "1");
    });
  }).catch(function (e) {
    statusEl.textContent = "Could not start the Glon runtime: " + (e && e.message || e);
  });
})();
