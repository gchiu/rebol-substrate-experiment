/* demo/shop/live-host.js -- browser host for the experimental GLON_LIVE
 * host-boundary probe (live.html + demo/shop/glon-live.wasm).
 *
 * This file is the thin browser half of the experiment:
 *
 *   - provide the five env imports: host_print, host_set_text, host_set_html,
 *     host_canvas_script and the new host_call;
 *   - load the three application <script type="application/glon"> blocks in
 *     order (common, strings, live);
 *   - forward [data-glon-event] clicks to glon_event;
 *   - on host_call("echo", arg), asynchronously (setTimeout) deliver a long
 *     UTF-8 reply through the new glon_event_bytes export.
 *
 * There is no Qwen, no microphone, no WebSocket here: this proves only the
 * generic outbound host_call + raw inbound glon_event_bytes round trip.
 */
(function () {
  "use strict";

  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();

  /* > 200 bytes of UTF-8, non-ASCII, including Chinese and emoji. */
  var REPLY =
    "这是一个用于验证 Glon 主机调用和原始字节事件往返的中文测试字符串。" +
    "它包含中文、日本語、한국어、emoji 😀🎤🌐 以及 ASCII 边界。" +
    "JavaScript 通过 host_call 收到 Glon 的请求后，异步地把这些字节送回 Glon，并由 Glon 渲染到页面上。";

  function view() {
    return new Uint8Array(ex.memory.buffer);
  }

  var imports = {
    env: {
      host_print: function (ptr, len) {
        console.log("[glon]", dec.decode(view().subarray(ptr, ptr + len)));
      },
      host_set_text: function (handle, value) {
        var el = document.querySelector('[data-glon-id="' + handle + '"]');
        if (el) el.textContent = String(value);
      },
      host_set_html: function (handle, ptr, len) {
        var el = document.querySelector('[data-glon-id="' + handle + '"]');
        if (el) el.innerHTML = dec.decode(view().subarray(ptr, ptr + len));
      },
      host_canvas_script: function () { /* this probe draws no canvas */ },
      host_call: function (opPtr, opLen, argPtr, argLen) {
        var op = dec.decode(view().subarray(opPtr, opPtr + opLen));
        var arg = dec.decode(view().subarray(argPtr, argPtr + argLen));
        if (op === "echo") {
          /* Asynchronous reply: the current WASM call must finish first. */
          setTimeout(function () { sendReply(arg); }, 0);
        } else {
          console.error("live-host.js: unknown host_call op '" + op + "' arg '" + arg + "'");
        }
      }
    }
  };

  function alloc(str) {
    var bytes = enc.encode(str);
    var p = ex.glon_alloc(bytes.length);
    view().set(bytes, p);
    return [p, bytes.length];
  }

  function glonEvent(token) {
    var pair = alloc(token);
    var rc = ex.glon_event(pair[0], pair[1]);
    if (rc !== 0) console.error("live-host.js: glon_event('" + token + "') rc=" + rc);
  }

  function sendReply(arg) {
    var bytes = enc.encode(REPLY);
    var token = alloc("reply");
    var data = ex.glon_alloc(bytes.length);
    view().set(bytes, data);
    var rc = ex.glon_event_bytes(token[0], token[1], data, bytes.length);
    if (rc !== 0) console.error("live-host.js: glon_event_bytes rc=" + rc + " (echo arg '" + arg + "')");
  }

  function boot() {
    if (ex.glon_init() !== 0) {
      console.error("live-host.js: glon_init failed");
      return;
    }
    var blocks = document.querySelectorAll('script[type="application/glon"]');
    for (var i = 0; i < blocks.length; i++) {
      var pair = alloc(blocks[i].textContent);
      if (ex.glon_load(pair[0], pair[1]) !== 0) {
        console.error("live-host.js: glon_load failed for block " + i);
        return;
      }
    }

    document.addEventListener("click", function (e) {
      var el = e.target && e.target.closest ? e.target.closest("[data-glon-event]") : null;
      if (!el) return;
      e.preventDefault();
      glonEvent(el.getAttribute("data-glon-event"));
    });

    glonEvent("init");   /* do-event falls through to render-current */
  }

  function ready(result) { ex = result.instance.exports; boot(); }
  function fail(err) { console.error("live-host.js: failed to load glon-live.wasm", err); }
  function loadBytes(bytes) { WebAssembly.instantiate(bytes, imports).then(ready).catch(fail); }

  if (typeof WebAssembly.instantiateStreaming === "function" && window.fetch) {
    WebAssembly.instantiateStreaming(fetch("glon-live.wasm"), imports)
      .then(ready)
      .catch(function () {
        fetch("glon-live.wasm").then(function (r) { return r.arrayBuffer(); }).then(loadBytes).catch(fail);
      });
  } else if (window.fetch) {
    fetch("glon-live.wasm").then(function (r) { return r.arrayBuffer(); }).then(loadBytes).catch(fail);
  } else {
    fail(new Error("no fetch / WebAssembly support"));
  }
})();
