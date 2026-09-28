/* demo/shop/live-host.js -- browser/device host for live.glon.
 *
 * Everything below the Glon semantic horizon lives here:
 *   - the microphone (getUserMedia + Web Audio + PCM16/16k downsampling);
 *   - the authenticated live-translate Python relay WebSocket;
 *   - the relay JSON protocol mapping.
 *
 * Glon sees only semantic intents (`listen-start`, `listen-stop`) and semantic
 * events (`qwen-status`, `qwen-source-*`, `qwen-text-*`, `qwen-error`) through
 * host_call / glon_event_bytes.  Microphone audio never passes through Glon and
 * Glon never sees base64, WebSocket or Qwen JSON.
 *
 * Relay URL: set window.LIVE_QWEN_WS_URL before this script runs, or open
 * live.html?relay=wss://localhost:8000/audio.  The relay is the proven
 * live-translate operator server (operator/server.py), which adds
 * Authorization: Bearer $DASHSCOPE_API_KEY and opens the permanent target=zh
 * and target=en Qwen sessions.  The microphone page is capture-only: the relay
 * fans the same PCM out to both sessions and audience devices display the
 * translation.  A browser cannot set that header itself.
 */
(function () {
  "use strict";

  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();

  var qwen = null;
  var stream = null;
  var context = null;
  var sourceNode = null;
  var processor = null;
  var failed = false;

  function view() {
    return new Uint8Array(ex.memory.buffer);
  }

  function imports() {
    return {
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
        host_canvas_script: function () { /* no canvas */ },
        host_call: function (opPtr, opLen, argPtr, argLen) {
          var op = dec.decode(view().subarray(opPtr, opPtr + opLen));
          var arg = dec.decode(view().subarray(argPtr, argPtr + argLen));
          onHostCall(op, arg);
        }
      }
    };
  }

  function alloc(str) {
    var bytes = enc.encode(str);
    var p = ex.glon_alloc(bytes.length);
    view().set(bytes, p);
    return [p, bytes.length];
  }

  /* deliver a semantic host event to Glon outside any in-flight WASM call */
  function deliver(token, text) {
    setTimeout(function () {
      var t = alloc(token);
      var d = alloc(text == null ? "" : String(text));
      var rc = ex.glon_event_bytes(t[0], t[1], d[0], d[1]);
      if (rc !== 0) console.error("live-host.js: glon_event_bytes('" + token + "') rc=" + rc);
    }, 0);
  }

  function glonEvent(token) {
    var pair = alloc(token);
    var rc = ex.glon_event(pair[0], pair[1]);
    if (rc !== 0) console.error("live-host.js: glon_event('" + token + "') rc=" + rc);
  }

  /* ---- microphone (device layer, below Glon) ---------------------------- */

  function downsampleTo16k(input, inputRate) {
    var outputRate = 16000;
    if (inputRate === outputRate) return input;
    var ratio = inputRate / outputRate;
    var length = Math.round(input.length / ratio);
    var result = new Float32Array(length);
    for (var i = 0; i < length; i++) {
      var pos = i * ratio;
      var left = Math.floor(pos);
      var right = Math.min(left + 1, input.length - 1);
      var frac = pos - left;
      result[i] = input[left] * (1 - frac) + input[right] * frac;
    }
    return result;
  }

  function floatToPCM16(float32) {
    var buffer = new ArrayBuffer(float32.length * 2);
    var view = new DataView(buffer);
    for (var i = 0; i < float32.length; i++) {
      var s = Math.max(-1, Math.min(1, float32[i]));
      s = s < 0 ? s * 32768 : s * 32767;
      view.setInt16(i * 2, s, true);
    }
    return buffer;
  }

  function updateMeter(input) {
    var el = document.getElementById("mic-level");
    if (!el) return;
    var sum = 0;
    for (var i = 0; i < input.length; i++) sum += input[i] * input[i];
    var rms = Math.sqrt(sum / input.length);
    var pct = Math.round(rms * 320);
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    el.style.width = pct + "%";
  }

  function clearMeter() {
    var el = document.getElementById("mic-level");
    if (el) el.style.width = "0%";
  }

  function startMicrophone() {
    return navigator.mediaDevices.getUserMedia({
      audio: {
        channelCount: 1,
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true
      }
    }).then(function (s) {
      stream = s;
      context = new AudioContext();
      var inputRate = context.sampleRate;
      sourceNode = context.createMediaStreamSource(stream);
      processor = context.createScriptProcessor(4096, 1, 1);
      sourceNode.connect(processor);

      /* the proven page connected straight to destination; use zero gain so
       * the microphone is not echoed to the speakers */
      var mute = context.createGain();
      mute.gain.value = 0;
      processor.connect(mute);
      mute.connect(context.destination);

      processor.onaudioprocess = function (event) {
        if (!qwen) return;
        var input = event.inputBuffer.getChannelData(0);
        var pcm = floatToPCM16(downsampleTo16k(input, inputRate));
        qwen.sendAudio(pcm);
        updateMeter(input);
      };
    });
  }

  function stopMicrophone() {
    if (processor) {
      processor.onaudioprocess = null;
      try { processor.disconnect(); } catch (e) {}
      processor = null;
    }
    if (sourceNode) {
      try { sourceNode.disconnect(); } catch (e) {}
      sourceNode = null;
    }
    if (stream) {
      stream.getTracks().forEach(function (track) { track.stop(); });
      stream = null;
    }
    if (context) {
      try { context.close(); } catch (e) {}
      context = null;
    }
    clearMeter();
  }

  /* ---- semantic intents (Glon -> device layer) -------------------------- */

  function relayUrl() {
    if (window.LIVE_QWEN_WS_URL) return window.LIVE_QWEN_WS_URL;
    if (window.location && window.location.search) {
      var v = new URLSearchParams(window.location.search).get("relay");
      if (v) return v;
    }
    return null;
  }

  function setStatus(state, text) {
    var app = document.getElementById("app");
    if (app) app.setAttribute("data-state", state);
    deliver("qwen-status", text);
  }

  function startListening() {
    if (qwen) return;

    var url = relayUrl();
    if (!url) {
      console.error("live-host.js: no relay URL; set window.LIVE_QWEN_WS_URL or ?relay=");
      setStatus("error", "Error");
      deliver("qwen-error", "Relay not configured");
      return;
    }

    setStatus("starting", "Starting…");
    failed = false;

    qwen = QwenClient.create({
      onEvent: function (token, text) {
        if (token === "qwen-status") {
          /* The relay names its internal target languages; this capture-only
           * page never shows them.  Surface only a clean terminal stop. */
          if (/stopped/i.test(text) && !failed) setStatus("stopped", "Stopped");
          return;
        }
        if (token === "qwen-error") {
          failed = true;
          setStatus("error", "Error");
          deliver(token, text);
          return;
        }
        deliver(token, text);
      },
      onOpen: function () {
        startMicrophone().then(function () {
          setStatus("listening", "Listening");
        }).catch(function (err) {
          console.error("live-host.js: microphone error", err);
          failed = true;
          stopListening();
          setStatus("error", "Error");
          deliver("qwen-error", "Microphone unavailable");
        });
      }
    });
    qwen.open(url);
  }

  function stopListening() {
    stopMicrophone();
    if (qwen) { qwen.close(); qwen = null; }
    setStatus("stopped", "Stopped");
  }

  function onHostCall(op, arg) {
    if (op === "listen-start" || op === "qwen-connect") { startListening(); return; }
    if (op === "listen-stop" || op === "qwen-close") { stopListening(); return; }
    console.error("live-host.js: unknown host_call op '" + op + "' arg '" + arg + "'");
  }

  /* ---- boot ------------------------------------------------------------- */

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
  function loadBytes(bytes) { WebAssembly.instantiate(bytes, imports()).then(ready).catch(fail); }

  if (typeof WebAssembly.instantiateStreaming === "function" && window.fetch) {
    WebAssembly.instantiateStreaming(fetch("glon-live.wasm"), imports())
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
