# Glon Host Capabilities — Project Memory
**Date:** 4 October 2026

## Core direction

Glon is now heading toward a universal application architecture:

> **Glon everywhere = one application, many tiny native hosts.**

The application logic and GUI are intended to remain the same across Windows, Linux, Android, HarmonyOS, macOS/iOS and future platforms.

The platform-specific component is a small native Glon host. The platform-independent component is browser/WASM Glon.

Conceptually:

```text
          SAME GLON APPLICATION
                  |
        +---------+---------+
        |                   |
   Native Glon          Browser Glon
   OS host/runtime          WASM
        |                   |
   OS capabilities      browser/View
        |                   |
        +-------- link ------+
```

The browser is Glon's View system. Glon does not need to reproduce a GUI toolkit, widget library, font system, Unicode handling, accessibility stack, multimedia stack, etc.

The native host supplies only capabilities that genuinely require access to
the underlying operating system.

In this document, **capability** has a narrow meaning: a semantic operation the
host can implement on a platform. It is not a Glon language module, not an app
permission grant, and not a commercial entitlement.

```text
host capability  = what the host can implement
permission       = what an app may be allowed to use
module           = functionality/code loaded by the app
entitlement      = licensed functionality the user may activate
```

## Proven desktop milestone

The D6 desktop architecture was committed, tagged and pushed on 4 October 2026.

- Commit: `d5c4afa3257de3e395a22642b437aa7bfd63274e`
- Tag: `glon-desktop-d6`
- Branch at milestone: `glon-shop-g1a`
- Windows native executable size: about 265 KB
- Frozen S1 remains unchanged at `f90496c26dc45c7a387d8fc8639cd2781507d3d2`

The architecture has been tested on POSIX and native Windows.

A real Chromium E2E proof demonstrated:

```text
browser/WASM Glon
      |
      | logical request
      v
native Glon
      |
      | authorises resource/path
      v
native host/filesystem
      |
      | streams bytes
      v
browser
```

A 2,046,016-byte resource was streamed from the native filesystem to Chromium without the bulk data entering the Glon managed heap or JavaScript.

The current architectural rule is:

> **Glon decides.  
> The host moves bytes.  
> The browser presents them.**

## Two communication planes

### 1. Control plane

Used for small values, commands, state, logical resource names and application decisions.

The existing `GLON_LIVE` path is suitable for modest messages.

The old `glon_event_value` path was practically limited to about 200 bytes. The existing `glon_event_bytes` mechanism raises this into the low-kilobyte range, but the frozen S1 heap means it should deliberately remain a small-value/control mechanism.

Do not enlarge frozen S1 merely to carry large files.

### 2. Data plane

Used for bulk byte streams:

- files
- images
- audio
- video
- downloads
- large documents
- future microphone/audio streams

Large payloads should bypass the Glon heap and travel directly between the native host and browser/resource consumer.

Glon remains responsible for deciding what is authorised and what a logical resource means.

## Authority boundary

Browser input must never directly acquire arbitrary filesystem access.

For example, the desktop proof currently behaves as follows:

```text
greeting                    -> authorised small file
desktop/big.txt              -> rejected as a browser-supplied path
../../etc/passwd             -> rejected
large                        -> authorised logical resource
unknown resource             -> rejected
```

The logical mappings live in Glon application code.

C may receive a logical name in a request and pass it to Glon, but C must not contain application rules such as:

```text
"large" -> "desktop/big.txt"
```

The host only performs primitive operations on paths/resources that Glon has authorised.

## Universal host contract

Do not require every platform to implement every possible operating-system facility.

Define a **small universal core host contract** and optional capabilities.

The central principle is:

> **Standardise capabilities, not implementations.**

### Proposed core capabilities

These should exist, in some appropriate form, on essentially every Glon host:

```text
app/lifecycle
time/now
time/sleep
random/secure

file/app-read
file/app-write
file/stat
file/list

net/connect
local-view-channel

stream/read
stream/write

view/open
```

The exact primitive names are not frozen yet.

The important concepts are:

- lifecycle
- clock/timers
- secure randomness
- application-private storage
- outbound networking
- communication with browser/WASM View
- streaming large byte sequences
- ability to expose/open the View

### Potential core-or-platform capability

```text
net/listen
```

Desktop Glon currently uses a localhost listener on `127.0.0.1`.

On mobile systems, lifecycle/background restrictions may make a separate localhost server less convenient. The same Glon application model may instead use an embedded system WebView with a platform bridge.

Therefore localhost listening should not be allowed to dictate the entire portable application model.

## Optional host capabilities

Capabilities which are useful but cannot be assumed everywhere include:

```text
process/spawn
process/capture-output
shell/execute

file/arbitrary-read
file/arbitrary-write

notify/system
clipboard/native

serial
usb
bluetooth

open/external-url
open/folder

device-specific services
```

Desktop Windows/Linux/macOS can expose more of these than sandboxed mobile platforms.

An application should not silently assume they exist.

### Brokered services vs broad authority

A host implementation may internally reuse privileged OS machinery **without
exposing that machinery as an application permission**.

For example, the Glon Fetch download service internally launches `curl`, and
SHA-256 internally reuses `sha256sum`/`certutil`. Neither of those exposes
`process/spawn` to the application. The application asks for a logical
operation; the trusted host service chooses and runs the implementation.

`process/spawn` is **broad arbitrary child-process authority**. It is an
optional privileged permission for applications that genuinely need to run
arbitrary programs. It should not be required merely because a service
implementation happens to launch a helper executable.

This preserves the canonical distinction:

```text
modules       -> functionality
permissions   -> authority
entitlements  -> licensing
package       -> identity/integrity
```

and the rule:

> **A manifest requests authority; it never grants authority.**

> **An entitlement can unlock functionality, but it can never manufacture host authority.**

## Browser-owned capabilities

Where practical, capabilities already well supplied by the browser should remain browser responsibilities rather than native Glon responsibilities.

Examples:

```text
DOM / HTML / CSS
fonts and text layout
Unicode / IME
touch and pointer input
accessibility
Canvas / SVG / WebGL / WebGPU
microphone
camera
audio/video playback
printing
drag-and-drop
browser clipboard where permitted
```

This is deliberate reuse, not a deficiency.

The browser industry has already built Glon's cross-platform View layer.

## Capability discovery

Glon should eventually be able to discover host capabilities rather than assuming a particular operating system.

Conceptually:

```glon
host/capabilities
```

might return something such as:

```glon
[
    file/app
    net/connect
    net/listen
    stream/read
    stream/write
    view
    process/spawn
]
```

Applications should not declare raw host capabilities as authority. Instead,
the Glon app model declares **permissions** that are resolved against host
capabilities.

Conceptually:

```glon
permissions [
    file/app-read
    net/connect
    view/open
]
```

The host capability table answers whether those semantic operations exist on
the current platform; trusted host/user/policy state decides whether the
permission is actually granted.

The exact syntax/API is not yet decided and should not be prematurely frozen.

## Platform model

### Windows / Linux / macOS

Likely model:

```text
native Glon process
      |
localhost / host bridge
      |
ordinary browser
      |
WASM Glon View
```

The desktop D6 proof already implements this model on Windows and POSIX.

### Android / HarmonyOS / iOS

The same browser/WASM Glon application should remain reusable.

However, mobile lifecycle rules may make launching a separate external browser undesirable because the native process may be suspended/backgrounded.

Likely model:

```text
native Glon host
      |
system WebView / browser component
      |
same WASM Glon View/application
```

Thus the View implementation may differ while the Glon application remains the same.

This does not alter the portability goal:

> **same Glon application; tiny platform host changes.**

### Headless/appliance/server

Native Glon can run on an appliance or server while the View runs in a browser on another device.

This is particularly relevant to future Glon appliances and distributed systems.

## Installation model

The intended distribution model is one small platform installation plus reusable browser/WASM assets.

Conceptually:

```text
Windows:
    glon installer
      + native glon.exe
      + glon.wasm
      + core Glon libraries

Android:
    APK
      + native Glon host
      + same browser/WASM Glon

HarmonyOS:
    platform package
      + native Glon host
      + same browser/WASM Glon

Apple:
    app/package
      + native Glon host
      + same browser/WASM Glon
```

Glon applications should then be portable application packages/programs rather than separately rewritten platform applications.

Conceptually:

```text
glon translator.glon
glon downloader.glon
glon membership.glon
```

The exact packaging format is still to be designed.

## Immediate application implications

### Download application

A downloader is likely an easier first real application than the translation system.

It would exercise:

- browser GUI
- native process execution
- filesystem
- progress/state messages
- bulk streaming
- opening folders/files

Reusing an external downloader such as `yt-dlp` would require an optional `process/spawn` / captured-output capability rather than embedding YouTube extraction logic in Glon.

### Translation system

The translation system fits the same architecture but needs a browser-to-native bulk streaming path for microphone/audio data.

Likely direction:

```text
browser microphone
      |
      | streamed audio
      v
native Glon host
      |
      | outbound translation API
      v
translation service
      |
      | small transcript/translation events
      v
browser/WASM Glon
```

The browser should continue to own microphone capture. Native Glon should not acquire a Windows-specific audio subsystem merely for this application.

## Design rules to preserve

1. **Frozen S1 stays frozen.**
2. **Glon owns semantics, policy and authority.**
3. **Native hosts expose primitives/capabilities, not application logic.**
4. **JavaScript remains thin browser/transport glue.**
5. **Bulk data bypasses the Glon heap.**
6. **Reuse operating-system and browser facilities rather than rebuilding them.**
7. **Do not force desktop-only capabilities into the universal host contract.**
8. **Applications declare permissions; hosts expose capabilities. Do not conflate the two.**
9. **The same Glon/browser-WASM application should survive across platforms.**
10. **Port the host, not the application.**

## Relationship to the Glon app model

The host capability model and the app model are deliberately separate.

The host says what semantic operations it can implement:

```text
file/app-read
net/connect
process/spawn
view/open
```

The app manifest may request permissions for some of those operations, but the
manifest is not itself authority.

Effective authority is:

```text
HOST IMPLEMENTS
      ∩
PLATFORM ALLOWS
      ∩
APPLICATION REQUESTS
      ∩
USER / POLICY GRANTS
      =
APPLICATION GETS
```

Modules and entitlements are separate concerns handled by the Glon app model:

```text
modules       → functionality
permissions   → authority
entitlements  → licensing
package       → identity/integrity
```

Canonical rules:

> **A manifest requests authority; it never grants authority.**

> **An entitlement can unlock functionality, but it can never manufacture host authority.**

### Application identity and per-app grants

Permissions are granted to a **stable application identity**, not globally.

- A manifest **declares** an application id.  It never authenticates itself.
- Trusted host **installation state** binds an application id to an installed
  package; the manifest's declared id must match the installed id.
- Trusted **grants are scoped to an exact application id** (no wildcard, prefix,
  substring or global fallback).  Another application requesting the same
  permission does not inherit another app's grant.
- Application **name** is display metadata and has no authority meaning.
  Application **version** is not part of the authority principal.
- App-local storage is derived from the application id, never from a
  browser-supplied path.

> **Authority follows identity, not manifest text.**

### Semantic permission contracts (D9)

A trusted **capability catalogue** (`desktop/capabilities.conf`, outside every
package) defines the canonical meaning of each capability: `id`, `label`,
`class` (`brokered-service`/`scoped`/`general`/`privileged`), `risk`
(`low`/`medium`/`high`) and `allows` (semantic action).  `process/spawn` is
`privileged` / `high`.  Classes and risk are explanatory only; the exact id
governs enforcement.

The catalogue is **semantics only**, separate from host **implementation**:
effective authority requires the host to actually implement the capability
**and** the catalogue to describe it.  A catalogue entry with no host
implementation is never effective even if requested and granted; a host
implementation with no trusted catalogue metadata fails closed.

An application manifest may add a `purpose: <permission-id> <text>` explanation.
It is an **untrusted vendor claim**: it cannot change canonical semantics, and a
dishonest purpose changes nothing.  An **unknown capability fails closed** even
if it is both requested and granted.

> **Host defines. App explains. User or agent decides. Host enforces.**

Do not prematurely design every future host API. Add semantic capabilities only
when a real application needs them.

## Short formulation

> **Glon everywhere = one app, many hosts.**  
> Same Glon/browser-WASM application on Windows, Android, HarmonyOS, Apple and beyond; only the tiny native host changes.

And the implementation rules remain:

> **Glon decides. The host moves bytes. The browser presents them.**

> **Modules provide functionality. Permissions grant authority. Entitlements unlock licensed functionality. Packages establish identity and integrity.**
