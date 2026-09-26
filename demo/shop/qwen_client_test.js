// demo/shop/qwen_client_test.js -- headless verification of the relay
// capability adapter (demo/shop/qwen-client.js).
//
// It proves that the existing live-translate relay browser protocol
// (status/source/source_done/translation/error) is mapped to the semantic
// Glon events, and that raw PCM audio is passed straight through to the
// transport without ever being inspected or decoded here.
//
// The transport is a fake WebSocket, so this runs in CI without network, a
// microphone or an API key.
"use strict";

const QwenClient = require("./qwen-client.js");

function FakeSocket() {
  this.readyState = 1;
  this.sent = [];
  this.lastBinary = null;
}
FakeSocket.prototype.send = function (payload) {
  this.sent.push(payload);
  if (typeof payload !== "string") this.lastBinary = payload;
};
FakeSocket.prototype.close = function () {
  this.readyState = 3;
  if (this.onclose) this.onclose();
};
FakeSocket.prototype.emit = function (obj) {
  if (this.onmessage) this.onmessage({ data: JSON.stringify(obj) });
};

let socket = null;
let opened = false;
const events = [];

const client = QwenClient.create({
  connect: function () { socket = new FakeSocket(); return socket; },
  onEvent: function (token, text) { events.push([token, text]); },
  onOpen: function () { opened = true; }
});

function fail(msg) {
  console.error("QWEN_CLIENT_TEST FAIL: " + msg);
  process.exit(1);
}
function last() { return events[events.length - 1]; }
function eq(a, b, what) {
  if (JSON.stringify(a) !== JSON.stringify(b)) fail(what + ": " + JSON.stringify(a) + " != " + JSON.stringify(b));
}

client.open("wss://relay/audio");
if (!socket) fail("transport was not created");
if (socket.onopen) socket.onopen();
if (!opened) fail("onOpen was not called");

socket.emit({ "type": "status", "text": "Listening — target Chinese" });
eq(last(), ["qwen-status", "Listening — target Chinese"], "status mapping");

socket.emit({ "type": "source", "text": "I'm speaking" });
eq(last(), ["qwen-source-delta", "I'm speaking"], "source delta mapping");

socket.emit({ "type": "source_done", "text": "I'm speaking English. " });
eq(last(), ["qwen-source-done", "I'm speaking English. "], "source done mapping");

socket.emit({ "type": "translation", "text": "我在说" });
eq(last(), ["qwen-text-delta", "我在说"], "translation delta mapping");

socket.emit({ "type": "translation_done", "text": "我在说英语。" });
eq(last(), ["qwen-text-done", "我在说英语。"], "translation done mapping");

socket.emit({ "type": "error", "text": "401 InvalidApiKey" });
eq(last(), ["qwen-error", "401 InvalidApiKey"], "error mapping");

const frame = new Uint8Array([0, 1, 2, 255]);
client.sendAudio(frame);
if (socket.lastBinary !== frame) fail("PCM frame was not passed through unchanged");

client.close();
if (socket.readyState !== 3) fail("socket not closed");
eq(last(), ["qwen-status", "Stopped"], "close mapping");

console.log("QWEN_CLIENT_TEST PASS (relay protocol -> semantic qwen-* events; raw PCM passthrough)");
process.exit(0);
