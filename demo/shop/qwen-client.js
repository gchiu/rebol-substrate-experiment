/* demo/shop/qwen-client.js -- the authenticated-relay capability adapter.
 *
 * The real live-translate/Qwen path is:
 *
 *   live.glon (semantic intents)
 *       -> live-host.js (microphone + this adapter)
 *           -> existing live-translate Python relay /audio
 *               -> Qwen realtime (authentication + Qwen JSON + base64 audio)
 *                   -> Qwen text events
 *               <- relay browser JSON
 *           <- qwen-* semantic events
 *       <- glon_event_bytes
 *
 * This file speaks ONLY the relay's small browser protocol and maps it to the
 * existing Glon semantic events.  It owns no application semantics: it never
 * decides append vs replace and never touches the page.  Audio bytes are the
 * relay's PCM16/16k frames and never pass through Glon.
 *
 * The transport is injectable so the mapping can be tested headlessly with a
 * fake socket (see qwen_client_test.js).
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    root.QwenClient = factory();
  }
}(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  function create(options) {
    options = options || {};

    var connect = options.connect || function (url) { return new WebSocket(url); };
    var onEvent = options.onEvent || function () {};
    var onOpen = options.onOpen || function () {};

    var ws = null;

    function emit(token, text) {
      onEvent(token, text == null ? "" : String(text));
    }

    function handle(raw) {
      var msg;
      if (typeof raw !== "string") return;
      try { msg = JSON.parse(raw); } catch (e) { emit("qwen-error", "bad JSON from relay"); return; }

      switch (msg.type) {
      case "status":
        emit("qwen-status", msg.text || "");
        break;
      case "source":
        emit("qwen-source-delta", msg.text || "");
        break;
      case "source_done":
        emit("qwen-source-done", msg.text || "");
        break;
      case "translation":
        emit("qwen-text-delta", msg.text || "");
        break;
      case "translation_done":
        emit("qwen-text-done", msg.text || "");
        break;
      case "error":
        emit("qwen-error", msg.text || JSON.stringify(msg));
        break;
      default:
        break;
      }
    }

    function open(url) {
      ws = connect(url);
      ws.onmessage = function (event) { handle(event.data); };
      ws.onerror = function () { emit("qwen-error", "relay WebSocket error"); };
      ws.onclose = function () { emit("qwen-status", "Stopped"); };
      ws.onopen = function () { onOpen(); };
    }

    /* send one raw PCM16/16k frame to the relay; never exposed to Glon */
    function sendAudio(bytes) {
      if (ws && ws.readyState === 1) ws.send(bytes);
    }

    function close() {
      if (ws) ws.close();
    }

    return {
      open: open,
      sendAudio: sendAudio,
      close: close
    };
  }

  return { create: create };
}));
