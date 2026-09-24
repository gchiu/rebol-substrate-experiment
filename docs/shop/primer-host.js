/* demo/shop/primer-host.js -- the browser host for Saturnine, the Glonbook
 * shell; its first Glonbook is the Glon primer (primer.html, generated from
 * primer.txt by build-primer.py).
 *
 * Every Run executes the example's CURRENT text through the real Glon
 * WebAssembly runtime:
 *
 *   - glon.wasm is compiled ONCE; each Run instantiates it afresh, so every
 *     run starts from a clean machine (no namespace carried between examples
 *     or between runs: the page never approaches the 256-binding global limit);
 *   - glon_init, then glon_load the environment embedded in the page
 *     (bootstrap + case, plus the shipped task library for data-env="tasks");
 *   - glon_run(source): the runtime runs the program and formats its outcome
 *     (r0_s1_show_run, the code the native primer doc-tests check);
 *   - show what Glon printed (host_print) and that outcome text, verbatim.
 *
 * This file evaluates nothing: no Glon semantics, no precomputed results.
 * It only moves text between the page and the runtime.
 */
(function () {
  "use strict";

  var dec = new TextDecoder();
  var enc = new TextEncoder();
  var modulePromise = null;
  var statusEl = document.getElementById("status");

  function envSource(name) {
    var el = document.querySelector('script[type="application/glon"][data-env="' + name + '"]');
    return el ? el.textContent : null;
  }

  // One fresh instance per run. Printed lines are collected for that run only.
  function runProgram(source, env) {
    return modulePromise.then(function (mod) {
      var ex = null;
      var printed = [];
      function view() { return new Uint8Array(ex.memory.buffer); }
      var imports = {
        env: {
          host_print: function (ptr, len) {
            var s = dec.decode(view().subarray(ptr, ptr + len));
            s.split("\n").forEach(function (l) { if (l.length) printed.push(l); });
          },
          // a printed integer >= 1000000 arrives here (the WEB_SET_INT
          // protocol: handle * 1000000 + value); show it as the number printed
          host_set_text: function (handle, value) { printed.push(String(handle * 1000000 + value)); },
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
        if (ex.glon_init() !== 0) return { printed: printed, error: "glon_init failed" };
        var names = ["bootstrap", "case"].concat(env === "tasks" ? ["tasks"] : []);
        for (var i = 0; i < names.length; i++) {
          var src = envSource(names[i]);
          if (src === null) return { printed: printed, error: "missing environment block " + names[i] };
          var a = put(src);
          var rc = ex.glon_load(a[0], a[1]);
          if (rc !== 0) return { printed: printed, error: "environment " + names[i] + " failed to load (rc " + rc + ")" };
        }
        var before = printed.length;
        var s = put(source);
        if (ex.glon_run(s[0], s[1]) !== 0) return { printed: printed, error: "program too long" };
        var ptr = ex.glon_result_ptr();
        var len = ex.glon_result_len();
        return { printed: printed.slice(before), result: dec.decode(view().subarray(ptr, ptr + len)) };
      });
    });
  }

  function line(out, text, cls) {
    var span = document.createElement("span");
    if (cls) span.className = cls;
    span.textContent = text;
    out.appendChild(span);
    out.appendChild(document.createTextNode("\n"));
  }

  function show(out, r) {
    out.textContent = "";
    r.printed.forEach(function (l) {
      // "[dump] ..." is the runtime's own diagnostic line on a halt
      line(out, l, l.indexOf("[dump]") === 0 ? "diag" : null);
    });
    if (r.error) { line(out, "host error: " + r.error, "sin"); return; }
    var sin = r.result.indexOf("**") === 0;
    line(out, "=> " + r.result, sin ? "sin" : "res");
  }

  function wire(box) {
    var ta = box.querySelector("textarea");
    var run = box.querySelector("button.run");
    var reset = box.querySelector("button.reset");
    var out = box.querySelector(".out");
    var env = box.getAttribute("data-env");
    function go() {
      if (run.disabled) return;
      run.disabled = true;
      out.textContent = "running…";
      runProgram(ta.value, env).then(function (r) { show(out, r); })
        .catch(function (e) { out.textContent = ""; line(out, "host error: " + (e && e.message || e), "sin"); })
        .then(function () { run.disabled = false; });
    }
    run.addEventListener("click", go);
    reset.addEventListener("click", function () { ta.value = ta.defaultValue; out.textContent = ""; });
    ta.addEventListener("keydown", function (e) {
      if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); go(); }
    });
    run.disabled = false;
    reset.disabled = false;
  }

  function fetchBytes() {
    return fetch("glon.wasm").then(function (r) {
      if (!r.ok) throw new Error("glon.wasm: HTTP " + r.status);
      return r.arrayBuffer();
    });
  }

  modulePromise = (typeof WebAssembly.compileStreaming === "function"
    ? WebAssembly.compileStreaming(fetch("glon.wasm")).catch(function () {
        return fetchBytes().then(function (b) { return WebAssembly.compile(b); });
      })
    : fetchBytes().then(function (b) { return WebAssembly.compile(b); }));

  modulePromise.then(function (mod) {
    var names = WebAssembly.Module.exports(mod).map(function (e) { return e.name; });
    if (names.indexOf("glon_run") < 0) throw new Error("this glon.wasm predates glon_run");
    Array.prototype.forEach.call(document.querySelectorAll(".ex"), wire);
    statusEl.textContent = "Ready. Every example below is editable; Run executes it in a fresh Glon WebAssembly runtime.";
  }).catch(function (e) {
    statusEl.textContent = "Could not start the Glon runtime: " + (e && e.message || e);
  });
})();
