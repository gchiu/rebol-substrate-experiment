/* Glon Inflate -- browser host.
 *
 * The decoder is the unchanged Glon DEFLATE implementation. glon.wasm is the
 * standard seven-primitive shop runtime (not the experimental 12-op machine).
 * This file only parses input containers, builds a tiny Glon driver source,
 * and frames the bytes Glon prints. It does not decompress anything.
 */
(function () {
  "use strict";

  var LIBS = ["prelude.glon", "strings.glon", "strings-ext.glon",
              "f2buf.glon", "inflate.glon", "inflate-driver.glon"];
  var MAX_SOURCE = 16384;
  var SAMPLE_LIMIT = 3000; // compressed bytes that fit in the generated driver
  var textDecoder = new TextDecoder("utf-8");
  var strictDecoder = new TextDecoder("utf-8", { fatal: true });
  var encoder = new TextEncoder();
  var modulePromise, libsPromise, samples = [];
  var activeFile = null;
  var lastOutput = null;

  var $ = function (id) { return document.getElementById(id); };
  var sampleEl = $("sample"), formatEl = $("format"), encodingEl = $("encoding");
  var inputEl = $("input"), fileEl = $("file"), decodeEl = $("decode");
  var clearEl = $("clear"), statusEl = $("status"), noteEl = $("sample-note");
  var resultEl = $("result"), inBytesEl = $("in-bytes"), outBytesEl = $("out-bytes");
  var formatUsedEl = $("format-used"), decodeTimeEl = $("decode-time");
  var verifyEl = $("verify"), textOutEl = $("text-out"), hexOutEl = $("hex-out");
  var errorEl = $("result-error"), downloadEl = $("download");

  function setStatus(text, cls) {
    statusEl.textContent = text;
    statusEl.className = "status" + (cls ? " " + cls : "");
  }

  function fetchJSON(url) {
    return fetch(url).then(function (r) {
      if (!r.ok) throw new Error(url + ": HTTP " + r.status);
      return r.json();
    });
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

  // The runtime's srcbuf is 16 KiB. Comments and indentation are irrelevant to
  // Glon, so remove them here; the decoder source itself is unchanged.
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

  function bytesToHex(bytes, limit) {
    var n = Math.min(bytes.length, limit || 256), lines = [];
    for (var i = 0; i < n; i += 16) {
      var hex = [], ascii = "";
      for (var j = 0; j < 16; j++) {
        if (i + j < n) {
          var b = bytes[i + j];
          hex.push(("0" + b.toString(16)).slice(-2));
          ascii += (b >= 32 && b < 127) ? String.fromCharCode(b) : ".";
        } else { hex.push("  "); ascii += " "; }
      }
      lines.push(("000000" + i.toString(16)).slice(-6) + "  " +
                 hex.slice(0, 8).join(" ") + "  " + hex.slice(8).join(" ") +
                 "  |" + ascii + "|");
    }
    if (bytes.length > n) lines.push("… " + (bytes.length - n) + " more bytes");
    return lines.join("\n");
  }

  function base64ToBytes(s) {
    s = s.replace(/\s+/g, "");
    if (!s) return new Uint8Array(0);
    if (!/^[A-Za-z0-9+/]*={0,2}$/.test(s) || s.length % 4 === 1) throw new Error("invalid Base64 input");
    try {
      var bin = atob(s), out = new Uint8Array(bin.length);
      for (var i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
      return out;
    } catch (e) { throw new Error("invalid Base64 input"); }
  }
  function hexToBytes(s) {
    s = s.replace(/\s+/g, "");
    if (!s || s.length % 2 || !/^[0-9a-fA-F]+$/.test(s)) throw new Error("invalid hexadecimal input");
    var out = new Uint8Array(s.length / 2);
    for (var i = 0; i < out.length; i++) out[i] = parseInt(s.substr(i * 2, 2), 16);
    return out;
  }

  // Container framing only. The DEFLATE payload is always passed to Glon.
  function adler32(bytes) {
    var a = 1, b = 0;
    for (var i = 0; i < bytes.length; i++) { a = (a + bytes[i]) % 65521; b = (b + a) % 65521; }
    return ((b << 16) | a) >>> 0;
  }
  var crcTable = (function () {
    var t = new Uint32Array(256);
    for (var n = 0; n < 256; n++) {
      var c = n;
      for (var k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
      t[n] = c >>> 0;
    }
    return t;
  })();
  function crc32(bytes) {
    var c = 0xffffffff;
    for (var i = 0; i < bytes.length; i++) c = crcTable[(c ^ bytes[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }
  function unwrapZlib(bytes) {
    if (bytes.length < 6) throw new Error("zlib stream is too short");
    var cmf = bytes[0], flg = bytes[1];
    if ((cmf & 15) !== 8) throw new Error("zlib: compression method is not DEFLATE");
    if ((cmf >> 4) > 7) throw new Error("zlib: invalid window size");
    if (((cmf << 8) + flg) % 31 !== 0) throw new Error("zlib: invalid header check");
    if (flg & 0x20) throw new Error("zlib: preset dictionaries are not supported");
    var raw = bytes.slice(2, bytes.length - 4);
    var want = ((bytes[bytes.length - 4] << 24) | (bytes[bytes.length - 3] << 16) |
                (bytes[bytes.length - 2] << 8) | bytes[bytes.length - 1]) >>> 0;
    return { raw: raw, check: function (out) {
      var got = adler32(out);
      if (got !== want) throw new Error("zlib: Adler-32 mismatch (expected " +
        want.toString(16) + ", got " + got.toString(16) + ")");
    }};
  }
  function unwrapGzip(bytes) {
    if (bytes.length < 18 || bytes[0] !== 0x1f || bytes[1] !== 0x8b || bytes[2] !== 8)
      throw new Error("not a valid gzip stream");
    var flags = bytes[3], p = 10;
    if (flags & 4) { if (p + 2 > bytes.length) throw new Error("gzip: bad extra field"); p += 2 + (bytes[p] | (bytes[p + 1] << 8)); }
    if (flags & 8) { while (p < bytes.length && bytes[p] !== 0) p++; p++; }
    if (flags & 16) { while (p < bytes.length && bytes[p] !== 0) p++; p++; }
    if (flags & 2) p += 2;
    if (p > bytes.length - 8) throw new Error("gzip: truncated header");
    var raw = bytes.slice(p, bytes.length - 8);
    var crc = (bytes[bytes.length - 8] | (bytes[bytes.length - 7] << 8) |
               (bytes[bytes.length - 6] << 16) | (bytes[bytes.length - 5] << 24)) >>> 0;
    var isize = (bytes[bytes.length - 4] | (bytes[bytes.length - 3] << 8) |
                 (bytes[bytes.length - 2] << 16) | (bytes[bytes.length - 1] << 24)) >>> 0;
    return { raw: raw, check: function (out) {
      if (crc32(out) !== crc) throw new Error("gzip: CRC-32 mismatch");
      if ((out.length >>> 0) !== isize) throw new Error("gzip: ISIZE mismatch");
    }};
  }
  function unwrap(format, bytes) {
    if (format === "raw") return { raw: bytes, check: function () {} };
    if (format === "zlib") return unwrapZlib(bytes);
    if (format === "gzip") return unwrapGzip(bytes);
    throw new Error("unknown container format");
  }

  function put(ex, bytes) {
    var p = ex.glon_alloc(bytes.length);
    if (!p && bytes.length) throw new Error("glon_alloc failed");
    new Uint8Array(ex.memory.buffer).set(bytes, p);
    return [p, bytes.length];
  }

  function parseFrames(bytes) {
    var out = [], chunks = 0, done = false, need = false, error = "", i = 0;
    var decoder = new TextDecoder("latin1");
    while (i < bytes.length) {
      var nl = bytes.indexOf(10, i);
      if (nl < 0) throw new Error("incomplete Glon output frame");
      var line = decoder.decode(bytes.subarray(i, nl));
      if (/^O [0-9]+$/.test(line)) {
        var n = Number(line.slice(2));
        var start = nl + 1, end = start + n;
        if (end + 1 > bytes.length) throw new Error("truncated Glon output frame");
        for (var k = start; k < end; k++) out.push(bytes[k]);
        if (bytes[end] !== 10) throw new Error("missing Glon frame newline");
        chunks++; i = end + 1;
      } else if (line === "D") {
        done = true; i = nl + 1;
      } else if (line === "N") {
        need = true; i = nl + 1;
      } else if (line === "E") {
        error = error || "Glon DEFLATE error"; i = nl + 1;
      } else if (line.indexOf("[dump]") === 0) {
        error = line; i = nl + 1;
      } else if (line === "") {
        i = nl + 1;
      } else {
        throw new Error("unexpected Glon output: " + line);
      }
    }
    return { bytes: new Uint8Array(out), chunks: chunks, done: done, need: need, error: error };
  }

  async function runDecoder(raw, chunks) {
    var mod = await modulePromise;
    var libs = await libsPromise;
    if (raw.length > SAMPLE_LIMIT) throw new Error("input is too large for this 16 KiB-source demo (" +
      raw.length + " bytes; max " + SAMPLE_LIMIT + ")");
    var captured = [], ex = null;
    var view = function () { return new Uint8Array(ex.memory.buffer); };
    var imports = { env: {
      host_print: function (ptr, len) { captured.push.apply(captured, view().subarray(ptr, ptr + len)); },
      host_set_text: function () {},
      host_set_html: function () {},
      host_canvas_script: function () {}
    }};
    var t0 = performance.now();
    var instance = await WebAssembly.instantiate(mod, imports);
    ex = instance.exports;
    if (ex.glon_init() !== 0) throw new Error("glon_init failed");
    for (var i = 0; i < libs.length; i++) {
      if (ex.glon_load.apply(null, put(ex, encoder.encode(libs[i]))) !== 0)
        throw new Error(LIBS[i] + " failed to load");
    }
    function runSource(source) {
      if (encoder.encode(source).length >= MAX_SOURCE)
        throw new Error("generated Glon source exceeds the 16 KiB source buffer");
      var before = captured.length;
      var rc = ex.glon_run.apply(null, put(ex, encoder.encode(source)));
      if (rc !== 0) throw new Error("Glon rejected the generated program (rc " + rc + ")");
      var frames = parseFrames(new Uint8Array(captured.slice(before)));
      if (frames.error) throw new Error(frames.error);
      return frames;
    }
    var initMs = performance.now() - t0;
    var t1 = performance.now();
    var all = [], nchunks = 0, done = false;
    runSource("demo/browser-new");
    for (var c = 0; c < chunks.length; c++) {
      var off = 0;
      for (var q = 0; q < c; q++) off += chunks[q];
      var part = raw.subarray(off, off + chunks[c]);
      runSource("demo/browser-feed mk-string [ " + Array.prototype.join.call(part, " ") + " ]");
      for (;;) {
        var r = runSource("demo/browser-read 256");
        for (var j = 0; j < r.bytes.length; j++) all.push(r.bytes[j]);
        nchunks += r.chunks;
        if (r.done) { done = true; break; }
        if (r.need) break;
      }
      if (done) break;
    }
    if (!done) {
      runSource("demo/browser-finish");
      for (;;) {
        var r2 = runSource("demo/browser-read 256");
        for (var j2 = 0; j2 < r2.bytes.length; j2++) all.push(r2.bytes[j2]);
        nchunks += r2.chunks;
        if (r2.done) { done = true; break; }
        if (r2.need) throw new Error("decoder still needs input after finish");
      }
    }
    var decodeMs = performance.now() - t1;
    var result = textDecoder.decode(view().subarray(ex.glon_result_ptr(), ex.glon_result_ptr() + ex.glon_result_len()));
    if (!done) throw new Error(result && result.indexOf("**") === 0 ? result : "Glon did not report completion");
    return { bytes: new Uint8Array(all), chunks: nchunks, error: "",
             initMs: initMs, decodeMs: decodeMs, result: result };
  }

  function sampleById(id) {
    for (var i = 0; i < samples.length; i++) if (samples[i].id === id) return samples[i];
    return null;
  }
  function clearFile() {
    activeFile = null;
    fileEl.value = "";
  }
  function loadSample(sample) {
    if (!sample) { noteEl.textContent = ""; return; }
    clearFile();
    formatEl.value = sample.format;
    encodingEl.value = "base64";
    inputEl.value = sample.data;
    noteEl.textContent = sample.format.toUpperCase() + ", " + sample.bytes + " bytes" +
      (sample.id === "streaming" ? " · fed in " + sample.chunks.length + " chunks" : "");
  }
  function updateSampleNote() {
    var s = sampleById(sampleEl.value);
    if (s) { loadSample(s); } else noteEl.textContent = "";
  }

  function showError(message) {
    resultEl.hidden = false;
    resultEl.querySelector(".stats").hidden = true;
    textOutEl.textContent = "";
    hexOutEl.textContent = "";
    verifyEl.textContent = "";
    errorEl.hidden = false;
    errorEl.textContent = message;
    downloadEl.disabled = true;
    lastOutput = null;
  }

  function showResult(result, meta) {
    var bytes = result.bytes;
    lastOutput = bytes;
    resultEl.hidden = false;
    resultEl.querySelector(".stats").hidden = false;
    inBytesEl.textContent = String(meta.inputBytes);
    outBytesEl.textContent = String(bytes.length);
    formatUsedEl.textContent = meta.format.toUpperCase() +
      (meta.sampleId === "streaming" ? " · " + result.chunks + " chunks" : "");
    decodeTimeEl.textContent = "Glon decode " + result.decodeMs.toFixed(1) +
      " ms · init/load " + result.initMs.toFixed(1) + " ms";
    errorEl.hidden = !result.error;
    errorEl.textContent = result.error || "";
    downloadEl.disabled = false;
    verifyEl.textContent = "";
    var looksText = bytes.length === 0 || Array.prototype.every.call(bytes, function (b) {
      return b === 9 || b === 10 || b === 13 || b >= 32;
    });
    if (looksText) {
      try { textOutEl.textContent = strictDecoder.decode(bytes); }
      catch (e) { textOutEl.textContent = "(binary output)"; }
    } else textOutEl.textContent = "(binary output; use the hex preview or download)";
    hexOutEl.textContent = bytesToHex(bytes, 512) || "(no bytes)";
    if (meta.sample) {
      var want = base64ToBytes(meta.sample.expected_base64);
      var same = want.length === bytes.length && want.every(function (b, i) { return b === bytes[i]; });
      verifyEl.textContent = same ? "Matches the stored expected output (" +
        meta.sample.output_bytes + " bytes)." : "OUTPUT DOES NOT MATCH THE SAMPLE.";
      if (!same) errorEl.hidden = false, errorEl.textContent = "Output does not match the sample.";
    }
  }

  async function decode() {
    if (decodeEl.disabled) return;
    decodeEl.disabled = true;
    setStatus("Running the Glon DEFLATE decoder…", "busy");
    resultEl.hidden = true;
    await new Promise(function (r) { setTimeout(r, 20); });
    try {
      var sample = sampleById(sampleEl.value);
      var format = formatEl.value;
      var container, unpacked, chunks;
      if (activeFile) {
        container = new Uint8Array(activeFile.bytes);
      } else {
        if (!inputEl.value.trim()) throw new Error("paste Base64 or hexadecimal input, or choose a sample");
        container = encodingEl.value === "hex" ? hexToBytes(inputEl.value) : base64ToBytes(inputEl.value);
      }
      unpacked = unwrap(format, container);
      if (sample && !activeFile) chunks = sample.chunks.slice();
      else chunks = unpacked.raw.length ? [unpacked.raw.length] : [];
      if (chunks.reduce(function (a, b) { return a + b; }, 0) !== unpacked.raw.length)
        throw new Error("internal chunk plan does not cover the input");
      var result = await runDecoder(unpacked.raw, chunks);
      if (result.error) { showError(result.error); setStatus("Glon reported an error.", "bad"); return; }
      unpacked.check(result.bytes);
      showResult(result, { inputBytes: container.length, format: format, sample: sample, sampleId: sample ? sample.id : "" });
      setStatus("Decoded by Glon on S1: " + result.bytes.length + " bytes.", "ok");
    } catch (e) {
      showError(e && e.message ? e.message : String(e));
      setStatus("Could not decode.", "bad");
    } finally {
      decodeEl.disabled = false;
    }
  }

  function wire() {
    sampleEl.addEventListener("change", updateSampleNote);
    inputEl.addEventListener("input", function () {
      clearFile();
      sampleEl.value = "";
      noteEl.textContent = "";
    });
    encodingEl.addEventListener("change", function () { /* encoding is explicit */ });
    fileEl.addEventListener("change", function () {
      var f = fileEl.files && fileEl.files[0];
      if (!f) return;
      f.arrayBuffer().then(function (b) {
        activeFile = { name: f.name, bytes: b };
        inputEl.value = "";
        sampleEl.value = "";
        noteEl.textContent = f.name + " · " + b.byteLength + " bytes (file bytes)";
      }).catch(function (e) { showError("could not read file: " + e.message); });
    });
    decodeEl.addEventListener("click", decode);
    clearEl.addEventListener("click", function () {
      inputEl.value = ""; clearFile(); resultEl.hidden = true; setStatus(""); noteEl.textContent = "";
    });
    inputEl.addEventListener("keydown", function (e) {
      if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); decode(); }
    });
    downloadEl.addEventListener("click", function () {
      if (!lastOutput) return;
      var blob = new Blob([lastOutput], { type: "application/octet-stream" });
      var a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = "glon-inflate-output.bin";
      a.click();
      setTimeout(function () { URL.revokeObjectURL(a.href); }, 1000);
    });
  }

  async function start() {
    try {
      modulePromise = fetchBytes("glon.wasm").then(function (b) { return WebAssembly.compile(b); });
      libsPromise = Promise.all(LIBS.map(function (f) { return fetchText(f).then(minifyGlon); }));
      var data = await fetchJSON("samples.json");
      samples = data.samples;
      samples.forEach(function (s) {
        var o = document.createElement("option");
        o.value = s.id; o.textContent = s.name; sampleEl.appendChild(o);
      });
      await Promise.all([modulePromise, libsPromise]);
      var hello = sampleById("hello");
      sampleEl.value = "hello"; loadSample(hello);
      decodeEl.disabled = false;
      setStatus("Ready. Choose a sample or paste compressed bytes.", "");
    } catch (e) {
      setStatus("Could not initialise: " + (e && e.message || e), "bad");
      showError(e && e.message || String(e));
    }
  }

  wire();
  start();
})();
