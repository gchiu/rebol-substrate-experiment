/* Glon Parse browser host.
 *
 * The rule interpreter is the unchanged Glon parse.glon running in glon.wasm
 * on the frozen seven-primitive S1 core. This file only fetches sources, builds
 * a small Glon driver source, and decodes result/capture frames printed by
 * parse-driver.glon. It contains no rule matching, searching, repetition,
 * character-set membership or capture algorithm.
 */
(function () {
  "use strict";
  var LIBS = ["prelude.glon", "strings.glon", "strings-ext.glon",
              "parse.glon", "parse-driver.glon"];
  var MAX_SOURCE = 16384;
  var MAX_INPUT = 2000;
  var dec = new TextDecoder("utf-8");
  var enc = new TextEncoder();
  var modulePromise, libsPromise, examples = [];

  var $ = function (id) { return document.getElementById(id); };
  var exampleEl = $("example"), inputEl = $("input"), registryEl = $("registry");
  var ruleEl = $("rule"), allEl = $("all"), caseEl = $("case");
  var runEl = $("run"), clearEl = $("clear"), statusEl = $("status");
  var resultEl = $("result"), resultLineEl = $("result-line");
  var capturesEl = $("captures"), errorEl = $("result-error");
  var timeEl = $("result-time"), noteEl = $("example-note");

  function status(text, kind) {
    statusEl.textContent = text;
    statusEl.className = "status" + (kind ? " " + kind : "");
  }
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
  function fetchJSON(url) {
    return fetch(url).then(function (r) {
      if (!r.ok) throw new Error(url + ": HTTP " + r.status);
      return r.json();
    });
  }

  // Remove Glon comments and indentation only. The Glon source itself is not
  // rewritten; the runtime's source buffer is 16 KiB.
  function minifyGlon(source) {
    return source.split(/\r?\n/).map(function (line) {
      var out = "", inString = false;
      for (var i = 0; i < line.length; i++) {
        var c = line[i];
        if (c === '"') { inString = !inString; out += c; continue; }
        if (!inString && c === ";" && line[i + 1] === ";") break;
        out += c;
      }
      return out.trim();
    }).filter(Boolean).join("\n");
  }
  function bytesOf(text) { return enc.encode(text); }
  function bytesList(bytes) {
    var a = [];
    for (var i = 0; i < bytes.length; i++) a.push(bytes[i]);
    return a.join(" ");
  }
  function textOf(bytes) {
    try { return new TextDecoder("utf-8", { fatal: true }).decode(bytes); }
    catch (e) { return dec.decode(bytes); }
  }

  function parseFrames(bytes) {
    var out = { result: null, error: false, captures: [] };
    var latin = new TextDecoder("latin1");
    var i = 0;
    while (i < bytes.length) {
      var nl = bytes.indexOf(10, i);
      if (nl < 0) throw new Error("incomplete Glon output frame");
      var line = latin.decode(bytes.subarray(i, nl));
      i = nl + 1;
      if (/^R [01]$/.test(line)) {
        out.result = Number(line.slice(2));
      } else if (line === "E") {
        out.error = true;
      } else if (/^C [0-9]+$/.test(line)) {
        var n = Number(line.slice(2));
        if (i + n + 1 > bytes.length) throw new Error("truncated capture name");
        var name = textOf(bytes.subarray(i, i + n)); i += n;
        if (bytes[i] !== 10) throw new Error("missing capture-name newline");
        i++;
        var nl2 = bytes.indexOf(10, i);
        if (nl2 < 0) throw new Error("truncated capture value header");
        var code = latin.decode(bytes.subarray(i, nl2)); i = nl2 + 1;
        var value = null;
        if (code === "N") value = null;
        else if (/^V [0-9]+$/.test(code)) {
          var vn = Number(code.slice(2));
          if (i + vn + 1 > bytes.length) throw new Error("truncated capture value");
          value = textOf(bytes.subarray(i, i + vn)); i += vn;
          if (bytes[i] !== 10) throw new Error("missing capture-value newline");
          i++;
        } else throw new Error("bad capture value frame: " + code);
        out.captures.push({ name: name, value: value });
      } else {
        throw new Error("unexpected Glon output: " + line);
      }
    }
    if (out.result === null && !out.error) throw new Error("missing Glon result");
    return out;
  }

  async function runParser(input, registry, rule, all, caseMode) {
    var mod = await modulePromise, libs = await libsPromise;
    if (bytesOf(input).length > MAX_INPUT)
      throw new Error("input is too large for this playground (max " + MAX_INPUT + " UTF-8 bytes)");
    var source = [
      "web/reset",
      "b: mk-string [ " + bytesList(bytesOf(input)) + " ]",
      "web-r: judge [",
      "  web-rules: " + (registry.trim() || "[]"),
      "  web-rule: " + rule,
      "  p/parse-mode web-rules b web-rule " + (all ? "1" : "0") + " " + (caseMode ? "1" : "0"),
      "]",
      "web/report web-r"
    ].join("\n");
    if (enc.encode(source).length >= MAX_SOURCE)
      throw new Error("generated Glon source is too large (16 KiB source buffer)");
    var captured = [], ex = null;
    var view = function () { return new Uint8Array(ex.memory.buffer); };
    var imports = { env: {
      host_print: function (ptr, len) { captured.push.apply(captured, view().subarray(ptr, ptr + len)); },
      host_set_text: function () {}, host_set_html: function () {}, host_canvas_script: function () {}
    }};
    var t0 = performance.now();
    var inst = await WebAssembly.instantiate(mod, imports); ex = inst.exports;
    if (ex.glon_init() !== 0) throw new Error("glon_init failed");
    function put(text) {
      var b = enc.encode(text), p = ex.glon_alloc(b.length);
      if (!p && b.length) throw new Error("glon_alloc failed");
      view().set(b, p); return [p, b.length];
    }
    for (var i = 0; i < libs.length; i++) {
      if (ex.glon_load.apply(null, put(libs[i])) !== 0)
        throw new Error(LIBS[i] + " failed to load");
    }
    var initMs = performance.now() - t0;
    var before = captured.length;
    var t1 = performance.now();
    var rc = ex.glon_run.apply(null, put(source));
    var runMs = performance.now() - t1;
    if (rc !== 0) throw new Error("Glon rejected the generated program (rc " + rc + ")");
    var result = parseFrames(new Uint8Array(captured.slice(before)));
    result.initMs = initMs; result.runMs = runMs;
    return result;
  }

  function showResult(r, inputBytes) {
    resultEl.hidden = false;
    errorEl.hidden = true; errorEl.textContent = "";
    resultLineEl.textContent = r.error ? "Glon parse error" :
      (r.result === 1 ? "Match: true" : "Match: false");
    resultLineEl.style.color = r.error ? "var(--bad)" : (r.result === 1 ? "var(--ok)" : "var(--bad)");
    timeEl.textContent = "init/load " + r.initMs.toFixed(1) + " ms · Glon run " +
      r.runMs.toFixed(1) + " ms · input " + inputBytes + " bytes";
    capturesEl.textContent = "";
    if (!r.captures.length) {
      var e = document.createElement("p"); e.className = "empty";
      e.textContent = "No captures."; capturesEl.appendChild(e);
    } else {
      r.captures.forEach(function (c) {
        var box = document.createElement("div"); box.className = "capture";
        var n = document.createElement("strong"); n.textContent = c.name;
        var v = document.createElement("code");
        v.textContent = c.value === null ? "none" : c.value;
        box.appendChild(n); box.appendChild(v); capturesEl.appendChild(box);
      });
    }
    if (r.error) {
      errorEl.hidden = false;
      errorEl.textContent = "The Glon parser raised an error. Check the rule data and input.";
    }
  }
  function showError(message) {
    resultEl.hidden = false;
    resultLineEl.textContent = "Could not run";
    resultLineEl.style.color = "var(--bad)";
    timeEl.textContent = "";
    capturesEl.textContent = "";
    errorEl.hidden = false; errorEl.textContent = message;
  }

  function loadExample(id) {
    var e = examples.find(function (x) { return x.id === id; });
    if (!e) return;
    inputEl.value = e.input;
    registryEl.value = e.rules;
    ruleEl.value = e.rule;
    allEl.checked = !!e.all;
    caseEl.checked = !!e.case;
    noteEl.textContent = e.note || "";
    resultEl.hidden = true;
  }
  function run() {
    if (runEl.disabled) return;
    runEl.disabled = true;
    status("Running the Glon parser…", "busy");
    resultEl.hidden = true;
    setTimeout(function () {
      runParser(inputEl.value, registryEl.value, ruleEl.value, allEl.checked, caseEl.checked)
        .then(function (r) {
          showResult(r, bytesOf(inputEl.value).length);
          status(r.error ? "Glon reported an error." : "Parser completed.", r.error ? "bad" : "ok");
        })
        .catch(function (e) {
          showError(e && e.message ? e.message : String(e));
          status("Could not run the parser.", "bad");
        })
        .finally(function () { runEl.disabled = false; });
    }, 20);
  }

  function wire() {
    exampleEl.addEventListener("change", function () { loadExample(exampleEl.value); });
    runEl.addEventListener("click", run);
    clearEl.addEventListener("click", function () { resultEl.hidden = true; status(""); });
    inputEl.addEventListener("keydown", function (e) {
      if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); run(); }
    });
  }

  function start() {
    try {
      modulePromise = fetchBytes("glon.wasm").then(function (b) { return WebAssembly.compile(b); });
      libsPromise = Promise.all(LIBS.map(function (f) { return fetchText(f).then(minifyGlon); }));
      fetchJSON("examples.json").then(function (j) {
        examples = j.examples;
        examples.forEach(function (e) {
          var o = document.createElement("option"); o.value = e.id; o.textContent = e.name;
          exampleEl.appendChild(o);
        });
        if (examples.length) { exampleEl.value = examples[0].id; loadExample(examples[0].id); }
        return Promise.all([modulePromise, libsPromise]);
      }).then(function () {
        runEl.disabled = false;
        status("Ready. Edit the input or encoded rule data and press Run.", "");
      }).catch(function (e) {
        status("Could not initialise: " + (e && e.message || e), "bad");
        showError(e && e.message || String(e));
      });
    } catch (e) {
      status("Could not initialise: " + e, "bad");
      showError(String(e));
    }
  }
  wire();
  start();
})();
