# GLON-APP-MODEL.md
**Draft architecture — 4 October 2026**

## Purpose

A Glon application should be portable across Windows, Linux, Android, HarmonyOS, Apple platforms and future hosts without being rewritten for each operating system.

The application model separates three concerns:

```text
GLON APP
   ├── modules       functionality/code the app needs
   ├── permissions   authority the app needs from the host
   └── package info  identity, version, signatures and licensing
```

This builds on the Glon desktop architecture:

> **Glon decides. The host moves bytes. The browser presents them.**

and on the portability goal:

> **One Glon application, many tiny native hosts.**

---

## 1. Modules

Modules define **what functionality is loaded into the application**.

Examples might include:

```text
integer
strings
float
decimal
json
crypto
sqlite
pdf
compression
image-codec
industrial-modbus
```

A Glon application should not load code it does not need.

For example, an application that uses only integers should not automatically load floating-point support.

Conceptually:

```glon
app [
    modules [
        integer
        strings
        json
    ]
]
```

The exact manifest syntax is not yet frozen.

### Module principles

- Modules affect **functionality and footprint**, not security authority.
- Modules should normally be resolved from a **local module store**.
- Starting an application should not normally require Internet access.
- Missing modules may be installed from a configured repository/store.
- Once installed, modules should be usable offline.
- Modules should be versioned.
- Module identity should eventually support content hashes and signatures.
- The module mechanism should allow free, open, commercial and private modules.
- Do not force every possible feature into the Glon core.

---

## 2. Permissions

Permissions define **what authority the application is allowed to exercise through the native host**.

Examples:

```text
file/app-read
file/app-write
file/arbitrary-read
file/arbitrary-write

net/connect
net/listen

process/spawn

open/external-url

notify/system
```

Conceptually:

```glon
app [
    permissions [
        file/app-read
        file/app-write
        net/connect
    ]
]
```

Permissions are not downloadable code and are not products.

They are host authority.

### Effective permission set

The authority available to an application is the intersection of:

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

An application should never need platform-specific tests such as:

```text
if windows [...]
if android [...]
```

Instead, startup resolves the requested permissions against the current host.

If a required permission is unavailable, startup should fail cleanly with a useful diagnostic.

Optional permissions may be supported later.

---

## 3. Package information

A Glon app package needs identity and integrity metadata.

Conceptually:

```text
name
publisher
version
entrypoint
view
modules
permissions
signature
```

Possible future module/package identity:

```text
module: pdf
version: 1.3.2
sha256: ...
publisher: ...
signature: ...
licence: ...
```

The exact format is not yet defined.

Do not prematurely freeze syntax until real applications have exercised the model.

---

## 4. Local module store

A Glon installation should contain or manage a **local module store**.

Conceptually:

```text
Glon installation
   ├── native host
   ├── glon.wasm
   ├── core libraries
   └── modules/
         ├── integer
         ├── strings
         ├── float
         ├── crypto
         ├── sqlite
         └── ...
```

Application startup should resolve modules locally:

```text
app manifest
     ↓
local module store
     ↓
load required modules
```

Normal execution should not require a remote repository.

---

## 5. Remote store / repository

A remote Glon Store or repository is an **installation and update source**, not a runtime dependency.

Conceptually:

```text
remote repository/store
        ↓
installer/package manager
        ↓
local module store
        ↓
application runs offline
```

If a required module is missing, the installer may obtain it from a configured source.

The application should not silently fetch executable code during ordinary startup.

### Possible store model

The store may distribute:

- free core/community modules;
- paid specialist modules;
- third-party commercial modules;
- signed/certified modules;
- enterprise/private modules;
- organisation-specific repositories.

Commercial access is to **modules/services**, not to permissions.

A permission such as `net/connect` is host authority and must never itself be treated as a purchased capability.

---

## 6. Licensing

Commercial modules may carry licence metadata.

A developer might distribute an application that requires:

```glon
modules [
    integer
    strings
    sqlite
    industrial-modbus
]

permissions [
    file/app-read
    file/app-write
    net/connect
]
```

If `industrial-modbus` is a licensed module, installation may obtain or validate the appropriate licence.

After installation and licence validation, normal application execution should preferably be able to proceed offline unless the licence model explicitly requires otherwise.

The Glon runtime itself should not be artificially crippled merely to create licensing opportunities.

---

## 7. Security model

Modules and permissions must remain conceptually separate:

```text
MODULE
    = code/functionality

PERMISSION
    = authority
```

Loading a module must not automatically grant authority.

For example:

```text
module: http-client
```

must not imply:

```text
permission: net/connect
```

The application must request the authority explicitly.

Likewise, possession of `process` helper code must not automatically grant `process/spawn`.

The host remains the final enforcement point.

### Brokered services do not leak broad authority

A host service may internally reuse privileged OS machinery without exposing
that machinery as an application permission.

```text
Glon Fetch app
      | logical fetch request
      v
fetch module / trusted host service
      | host implementation detail
      v
     curl  ->  authorised app download directory
```

Glon Fetch therefore requests `net/connect`, `file/app-write` and
`open/folder` — **not** `process/spawn`. The application cannot name or select
an executable; it only asks for the logical operation.

`process/spawn` is broad authority and is reserved for applications that
genuinely need arbitrary child processes. It must not be required merely
because a service implementation happens to launch a helper executable.

```text
modules       -> functionality
permissions   -> authority
entitlements  -> licensing
package       -> identity/integrity
```

---

## 8. Host relationship

The native Glon host exposes a set of semantic permissions/capabilities appropriate to the platform.

Examples:

```text
Windows host
    file/app-read
    file/app-write
    net/connect
    net/listen
    process/spawn
    view/open

Mobile host
    file/app-read
    file/app-write
    net/connect
    view/open
```

The application manifest remains platform-independent.

The host supplies different implementations while preserving the same semantic contract.

---

## 9. Browser/WASM View

The browser/WASM side remains the portable user-interface layer.

A Glon app package may therefore conceptually contain:

```text
myapp.glonapp
   ├── manifest
   ├── app.glon
   ├── view.glon
   ├── browser assets
   └── module requirements
```

The exact packaging format remains open.

The same application package should be usable with:

```text
Windows native Glon host
Android native Glon host
HarmonyOS native Glon host
Apple native Glon host
Linux native Glon host
```

provided the required modules and permissions are available.

---

## 10. Installation model

Installation resolves:

1. package identity/signature;
2. required modules;
3. module versions;
4. licensing requirements;
5. requested permissions;
6. host/platform compatibility.

Conceptually:

```text
install app
   ↓
verify package
   ↓
resolve modules locally
   ↓
fetch missing modules if authorised
   ↓
verify module hashes/signatures
   ↓
present/grant requested permissions
   ↓
install
   ↓
run offline
```

Runtime should be simpler:

```text
start app
   ↓
load local modules
   ↓
grant allowed permissions
   ↓
run
```

---

## 11. Example application manifests

### Downloader

Conceptually:

```glon
app [
    name "Downloader"

    modules [
        integer
        strings
        downloader-ui
    ]

    permissions [
        file/app-write
        net/connect
        process/spawn
        view/open
    ]
]
```

### Translation application

Conceptually:

```glon
app [
    name "Translator"

    modules [
        integer
        strings
        translation-client
    ]

    permissions [
        file/app-read
        file/app-write
        net/connect
        view/open
    ]
]
```

Microphone capture should normally remain a browser/View responsibility rather than requiring a Windows-specific native audio permission.

These examples are illustrative only; syntax is not final.

---

## 12. Design rules

1. **Keep Glon core small.**
2. **Load only the modules an application needs.**
3. **Modules are functionality; permissions are authority.**
4. **Applications should run from locally installed modules.**
5. **Remote repositories are for install/update, not normal execution.**
6. **The same application manifest should work across platforms.**
7. **Hosts expose semantic permissions, not OS API names.**
8. **The host enforces permissions.**
9. **Modules do not implicitly grant host authority.**
10. **Signed/content-addressed modules are preferred for future distribution.**
11. **Commercial licensing belongs to modules/services, not permissions.**
12. **Do not freeze syntax before real applications have tested the model.**

---

## 13. Short formulation

> **A Glon app declares what code it needs and what authority it needs.**

```text
modules      → functionality
permissions  → authority
package      → identity/integrity/licensing
```

The installer resolves modules and licences.

The host grants permissions.

The application then runs locally using the same Glon/browser-WASM code across platforms.

> **Port the host, not the app.**
