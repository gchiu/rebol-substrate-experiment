# Glon Developer Preview 1 — release announcement

**Glon — Developer Preview 1**

Glon is an experimental execution substrate designed primarily for
AI-generated applications.

This first developer preview demonstrates a compact interpreter, multitasking,
configurable memory profiles, file I/O, HTTPS application serving and JSON
processing.

A working HTTPS application requires just six lines and 125 bytes of Glon
source. The HTTPS and file-enabled executable occupies approximately 290 KiB,
excluding shared dependencies and supporting Glon source libraries.

Working applications, source code, AI-agent authoring documentation and
reproducible tests accompany the release.

Glon remains experimental. The next major engineering objectives are execution
efficiency, long-running reliability and a UniServe-inspired asynchronous,
multiprotocol networking substrate.

Humans specify intent. AI generates applications. Glon executes them.

---

## What is included

- Frozen seven-primitive S1 core and native interpreter.
- Constrained 64K and large 1,048,576-cell memory profiles.
- M1 cooperative multitasking (verified on the large Linux profile).
- File I/O vocabulary and host permissions.
- CivetWeb HTTPS transport with OpenSSL.
- HTTP, reusable web and general JSON vocabularies.
- Four verified HTTPS examples.
- Experimental membership register with NDJSON persistence.
- AI-agent guide (`AGENTS.md`), quick start, vocabulary inventory and manifest.
- Linux x86-64 and native Windows x86-64 binaries.

## Architecture

```text
Application expressed in Glon
             |
HTTP / Web / JSON vocabularies
             |
Glon execution substrate
             |
Native HTTP transport — CivetWeb
             |
TLS — OpenSSL
             |
Network
```

Routing, validation, response content/status, HTML/JSON generation and
persistence logic are Glon. CivetWeb/OpenSSL provide transport capabilities,
not application behaviour.

## Measured limits

| Records | NDJSON restore |
|---:|---:|
| 10 | 1.298 s |
| 100 | 18.438 s |
| 1,000 | 450.143 s |
| 10,000 | failed — return-stack exhaustion |

Completed endurance: 100 requests (34.04 s) and 1,000 requests (719.83 s,
median 388 ms, p95 883 ms). The 100,000-request target was not completed. Do not
treat short acceptance tests as proof of long-term reliability.

## Honest limitations

- HTTPS request handlers are serialised; concurrent Glon request handlers are
  not implemented.
- Keep-alive has known connection-starvation limitations.
- The 200-record membership restart passed; 10,000 restore failed.
- Restoration performance scales poorly.
- JSON nesting/collection limits remain.
- Some recursive operations can exhaust the return stack.
- Persistence does not guarantee durability against power failure.
- Authentication, authorisation, CSRF protection and production hardening are
  incomplete.
- Windows file-enabled membership persistence is not included because the file
  module has no Win32 backend.

## Next

**Beyond HTTPS — Towards a UniServe-inspired asynchronous networking substrate**

DocKimbel's UniServe for REBOL is the historical inspiration. Glon's next
networking objective is a reusable asynchronous, multiprotocol networking
substrate, composed with Glon multitasking and protocol vocabularies. Potential
applications include concurrent HTTPS, mail protocols, WebSockets, MQTT,
distributed nodes, embedded configuration and robotic diagnostics. These are
proposals, not implemented features. UniServe has not already been recreated
in Glon.
