# Optional native services and the HOST boundary

**Status:** architectural investigation only. No S1, HOST, evaluator, parser,
scheduler or I/O implementation change is proposed here as implemented.
**Branch:** `design/host-services`
**Base:** `72ff1ff99e5043ffd116b3b8f60f2d53e6d307e3` (IO-3 streaming baseline)
**Labels:** **[V]** verified in the sources below; **[I]** architectural
inference; **[P]** proposal / not implemented.

---

## 1. Executive conclusion

The closed HOST service table is a real constraint, but it is not the main
source of the IO-3 complexity. External file I/O needs asynchronous completion,
a host owner for descriptors, capability enforcement, request/reply state and
cleanup regardless of whether the service is reached through `HOST`, a generic
native dispatch or an out-of-process driver. Extending HOST would remove some
local framing and copying, but would not remove the worker/event model.

The strongest immediate target is the **bounded computational** path: Glon
substring search is currently ordinary Glon code and costs about 246 s per MiB
for the no-match case **[V]** (`GLON-OPTIONAL-IO-STREAMING.md:38,194`). A masm
implementation over the existing seven instructions is the lowest-risk
experiment and should be measured before changing the frozen machine.

Recommendation: **keep the HOST service table closed at `s1-frozen-v1` for
now.** Do not append per-service file or network IDs. If bounded native
services later demonstrate a measured need that masm cannot meet, treat a
versioned generic service dispatch as a **new machine version/tag**, with a
static descriptor table and a separate substrate decision, not as an incidental
extension of S1.

---

## 2. Existing HOST architecture

### 2.1 Seven opcodes, one inline service operand

**[V]** S1 has seven primitive instructions (`LIT`, `DUP`, `DROP`, `@`, `!`,
`0BRANCH`, `HOST`) plus the harness-only `OP_HALT` (`s1.h:30-40`). `HOST` is
emitted as the opcode followed by an inline service id
(`s1.c:48`; `s1.c:184`). The service id is therefore not another opcode; it is
an operand of the one `HOST` instruction.

**[V]** The current service ids are a closed enum from `HOST_ADD = 0` through
`HOST_DUMP = 15` (`s1.h:42-49`). `s1.c` implements that table in one C
`switch` (`s1.c:134-163`). The table is arithmetic, comparison, allocation,
byte output, integer print and a diagnostic dump. The header explicitly says
host functions must not manipulate `IP`, `SP`, `RP`, unwind or save/restore
execution state (`s1.h:42-44`).

**[I]** Thus the **instruction semantics** of `HOST` are "execute the inline
host service id". The **service semantics** are the per-id stack effects and
effects. Extending the table changes the service ABI, not the opcode set. The
distinction matters for the frozen-contract discussion in §8.

### 2.2 HOST is called from three broad layers

**[V] Assembler / masm:** `asm_host(id)` emits `OP_HOST, id` (`s1.c:42-48`),
and the arithmetic helpers expand to HOST ids (`s1.c:50-55`). `raw_validate`
recognises `HOST` as a mnemonic and rejects an id outside the closed range
`0..HOST_DUMP` before emitting anything (`r0_s1_runtime.c:469-475`,
`r0_s1_runtime.c:516-519`). The assembler path later emits `asm_host((int)v)`
(`r0_s1_runtime.c:587`).

**[V] R0 native trampoline:** Native ids share the arithmetic HOST ids
(`r0_s1.h:53-71`). `emit_native` evaluates native arguments, untags integers,
calls the corresponding HOST operation, tags the result and appends the R0
result arity (`r0_s1_runtime.c:3089-3173`, arithmetic chain at
`3213-3258`). The native trampoline is the bridge between R0's
`[values..., N]` result protocol and HOST's fixed raw-cell effects. The source
architecture states this explicitly: HOST never produces a R0 result frame,
and the trampoline reconstructs R0 arity (`R0-ARCHITECTURE.md:162-189`;
`R0-S1-PHASE1.md:96-103`).

**[V] RAW / masm:** `RAW` values are assembled S1 fragments and may contain
`OP_HOST` directly (`R0-ARCHITECTURE.md:662-687`). This is the generic
trapdoor. It is trusted, unsafe code, not a security boundary.

**[V] Browser/WASM:** HOST ids are not the browser ABI. The WASM module imports
`env.host_print`, `env.host_set_text`, `env.host_set_html`,
`env.host_canvas_script`; GLON_LIVE adds `env.host_call`
(`GLON-WASM-ABI.md:49-52,88-96`). The WASM host boundary is a separate ABI and
is not an extension of the S1 HOST enum.

### 2.3 The closed table in the frozen contract

**[V]** `s1.c`, `s1.h`, `tests.c`, `adversarial.c` and `claims.c` are frozen at
tag `s1-frozen-v1` (`f90496c…`); `./check-frozen-s1.sh` compares the files
byte-for-byte (`GLON-ARCHITECTURE-MEMORY.md:66-70`; `MEMORY.md:37-43`).
The frozen tests themselves use the derived macros and do not enumerate HOST
ids, but the **source hash** freezes `s1.h` and `s1.c` including the enum and
switch. **[V]**

**[V]** The invalid-id run-time behaviour is currently permissive: an unknown
id prints `s1: bad host id …` and continues (`s1.c:159-161`). The validator
prevents such ids from assembled masm (`r0_s1_runtime.c:516-519`), and the
machine-integrity audit records the old silent-run defect and its repair
(`MACHINE-INTEGRITY-AUDIT.md:139-143`). Any new service table must define an
explicit invalid/unsupported-status policy rather than inherit this default.

### 2.4 Why the table was closed: evidence versus inference

**[V] Documented decisions:**
- S1 has exactly seven primitives and no eighth (`GLON-ARCHITECTURE-MEMORY.md:66-70`;
  `MEMORY.md:37-40`).
- HOST is "mundane": arithmetic, comparison, allocation, I/O and diagnostics,
  and must never acquire evaluator or control semantics
  (`GLON-ARCHITECTURE-MEMORY.md:72-73`; `MEMORY.md:42-43`).
- The substrate is intended to be portable and host-independent; RAW is the
  generic escape (`DESIGN.md` "The Substrate Principle";
  `R0-ARCHITECTURE.md:162-169`).
- Mechanisms belong in the kernel; meanings belong in loadable Glon
  (`GLON-ARCHITECTURE-MEMORY.md:74-82`).

**[I] Technically essential consequences, not necessarily the original
motives:**
- A closed switch gives a finite, auditable set of raw-cell stack effects that
  the evaluator and verifier can reason about.
- It prevents the substrate from acquiring platform-specific facilities and
  keeps the same machine contract on POSIX, Windows, WASM and embedded hosts.
- It keeps invalid-id handling and binary layout simple: one inline cell, no
  service descriptor, no registration table, no pointer escaping into C.
- It is not essential to the **seven opcodes** that the service table be
  closed. A closed table is a separate ABI decision layered into the `HOST`
  instruction.

**[I]** The historical record does not contain a standalone "HOST must remain
closed" rationale beyond these decisions and the frozen-tag process. The
closed table is best treated as a deliberate contractual choice, not a logical
consequence of seven primitives.

---

## 3. IO-3 baseline: where the complexity actually is

**[V]** The streaming implementation's path is:

```text
Glon builds [id op fields]
  -> native driver io/take-request
  -> C copies fields into a v1 frame
  -> glon_io_decode_request validates
  -> glon_io_cli_submit copies the frame to host storage
  -> pthread worker performs POSIX open/read/write/close
  -> worker writes a reply frame, publishes done
  -> driver joins, checks request id/status
  -> io/reply rebuilds a transient Glon source expression
  -> Glon callback queues the next request
```

Relevant sources: `modules/file.glon` request/pending/reply
(`modules/file.glon:61-134`), `modules/file-stream.glon` streaming state
(`modules/file-stream.glon:126,166`), native extraction and draining
(`glon.c:243-329,441-503`), and the worker/driver
(`glon_io_file_cli.c:41-67,99-...`). The v1 frame is documented at
`glon_io_file.h:18-29`.

**[V]** The path includes more than HOST restriction:
- a worker thread and `pthread_create`/`join`;
- an event/poll loop and one-outstanding state;
- a frame codec and status table;
- managed/loader-heap lifetime management for request/reply values;
- path/permission policy and handle generations;
- cleanup and cancellation.

**[I]** A generic synchronous HOST operation cannot replace this path for
filesystem access. A HOST call is inside the Glon execution thread; blocking
there blocks unrelated tasks. A non-blocking file operation still needs a
completion channel and scheduler/driver state. Removing HOST's closedness
would remove some **local transport** and call overhead, not the asynchronous
design.

**[V]** The streaming search itself is ordinary Glon, not a HOST call
(`modules/file-stream.glon:18-64`). Its low throughput is an algorithmic /
interpreter cost, not a request/reply framing cost: the measured 1 MiB
no-match run is about 246 s (`GLON-OPTIONAL-IO-STREAMING.md:38,194`). This is
the strongest case for a **bounded computational** acceleration service or a
masm implementation.

---

## 4. Four alternative architectures

### A. Existing IO-3 architecture — baseline

**Mechanism.** Glon keeps explicit request state, the host extracts a structured
request, a worker performs the operation, and the host injects a reply. The v1
frame remains the cross-host contract. The Glon library owns state-machine
sequencing and callbacks.

**`s/find text pattern`.** Currently the library implements the search in Glon
(`stream/find-first`/`find-range`/`find-small`). There is no service call. The
cost is Glon/S1 scanning; no frame or worker is involved.

**File read.** The complete IO-3 path above. It is asynchronous and supports
unrelated host-dispatched work while the worker is pending.

**Assessment.**
- **[V]** Managed-heap and loader-heap lifetime are explicit; GC is bounded in
  the measured streaming runs.
- **[V]** Capability policy is separate from compilation.
- **[V]** The protocol is portable in principle and is useful for distributed
  nodes.
- **[I]** Local framing, source reconstruction, worker creation and callback
  state are pure local overhead. IO-3 has already removed one large cost by
  making `io/take-request` structured and by reclaiming transient loader
  blocks (`glon.c:243-329`).
- **[V]** It is POSIX/pthread-specific today and has no Windows/WASM/embedded
  backend.

### B. Extensible HOST service table

**Mechanism.** Keep the seven opcodes and all existing HOST ids/semantics.
Append explicitly registered optional service ids after 15, compiled in only
for selected modules. A minimal build omits the code and rejects the ids.

**`s/find text pattern`.** A masm wrapper or optional native would put raw
string pointers/lengths/service id on the stack and execute `HOST_FIND`.
The C service scans and returns an offset/status without allocating. No frame,
worker or reply reconstruction. The ordinary Glon vocabulary would be a
library wrapper over the masm/service call, not automatic polymorphism.

**File read.** A synchronous `HOST_READ` would block the Glon thread and is
unacceptable. A non-blocking `HOST_IO_SUBMIT`/`HOST_IO_POLL` still needs a
driver/event loop and pending-job state; it does not eliminate A's
asynchronous machinery, though it could remove the frame and reply-source
encoding for local calls.

**Frozen impact.** This requires changing `s1.h` and `s1.c`, or creating a new
machine version. Appending after 15 preserves old ids/semantics; inserting
before `HOST_DUMP` shifts them and is not acceptable. Changing `HOST_DUMP` as
the validator's maximum requires a new bound constant and validator update.
Existing source hashes cannot be preserved.

**Assessment.**
- **[I]** Lowest native-call overhead and copying for bounded operations.
- **[I]** High architectural risk: platform-specific facilities enter the
  substrate; every S1 implementation and every copied host switch must be
  updated.
- **[V]** There are duplicate S1/host implementations in experiment copies
  (for example `s1_prof.c:160-184` and
  `experiments/glon-deflate-b11/b11_machine.c:173-197`), so a HOST contract
  change is not local.
- **[I]** GC safety is the hard part: a C service must not allocate, trigger
  GC, retain a raw managed pointer or let the collector move values while it
  holds them.
- **[I]** Not suitable as an append-only design for file/network services;
  those still need async completion.

### C. One generic versioned service dispatch

**Mechanism.** Add one service operation (for example `HOST_SERVICE`) whose
inline operand or stack arguments select a versioned, statically registered
service descriptor. The descriptor carries service id/version, argument and
result schema, class (sync/async), capability and stack effect. No function
pointers or arbitrary C calls from Glon; the host dispatches through a static
table and validates every field.

**`s/find`.** `HOST_SERVICE(find, version, text descriptor, pattern descriptor,
bounds)` calls a C scanner and returns offset/status. One dispatch and one
validation layer replace per-service ids. No frame, worker or callback.

**File read.** A generic async submit/poll can be expressed, but still
requires an event loop, request IDs and pending state. The generic operation
does not make file I/O synchronous or remove the worker model. It could make
local and remote transports share a service id/schema.

**Frozen impact.** Still changes the HOST service contract/source hashes unless
implemented as a new machine version. A generic operation needs a defined
calling convention and stack-effect descriptor; the frozen evaluator and
`raw_validate` cannot infer arbitrary service arity from a stack of cells
without such a descriptor. A bad descriptor or unregistered service must
fail closed, not mimic the current permissive bad-id path.

**Assessment.**
- **[I]** Better than B for extensibility: one auditable dispatch, common
  version/capability/status model, less enum churn.
- **[I]** More complex ABI and verifier surface: schemas, version negotiation,
  arity/result rules, status propagation, runtime registry validation.
- **[I]** Still not a solution for blocking external I/O; async class must have
  a driver.
- **[I]** Could later unify local and distributed service contracts, with a
  local adapter and a remote framed adapter.

### D. Optional evaluator-level native service above S1

**Mechanism.** Leave S1 and HOST untouched. Put acceleration in ordinary
Glon/masm library code, or provide a service above the evaluator that the
evaluator can reach without an S1 host call.

**`s/find`.** A masm library word scans the managed string with `@`/`!`/HOST
arithmetic and returns an offset. It uses only the existing machine. A
portable Glon implementation can sit behind the same `s/find` contract. This
is a real option and requires no frozen change.

**File read.** Since the evaluator is emitted S1 and can reach C only through
HOST, a true synchronous C file service has no legal path. The existing
host-mediated asynchronous driver remains. If D instead means a C callback
contract, it collapses into B/C or a new host-call contract; it is no longer
"above S1".

**Assessment.**
- **[V/I]** Best fit for bounded computational work: portable, no ABI change,
  no pointer ownership transfer, no worker.
- **[I]** S1 instruction count limits throughput; measure before assuming a
  native service is necessary.
- **[V]** External I/O remains A; D does not solve asynchronous completion.

---

## 5. Synchronous computational vs asynchronous external services

**[I]** A service class distinction is necessary; one mechanism for both would
force workers or callbacks onto trivial operations and would obscure the
blocking rule.

| Property | Bounded computational | External I/O |
|---|---|---|
| Examples | find/compare/checksum, bounded byte scan | file, network, device |
| Duration | bounded by caller-supplied limits | can block indefinitely |
| Completion | synchronous return is acceptable | must be asynchronous or host-event-driven |
| Worker | not needed | needed or host event loop |
| State | arguments + result | request id, handles, permissions, cleanup |
| Failure | status/range/timeout | status + cancellation + partial effects |
| GC | must not allocate/GC while holding raw pointers | replies are copied into managed values |
| Determinism | deterministic if input/bounds fixed | depends on external state |

**Shared model.** Both can share a **service descriptor** and a **capability
grant**: id, contract version, class, argument schema, result/status schema,
permission name, resource bounds and implementation identity. The execution
adapter differs:
- sync class: direct call with validated bounded arguments and a fixed result;
- async class: submit/accept request id, host completion, reply mapping,
  cleanup and cancellation.

The descriptor does not make a sync operation safe to block, and does not make
an async operation safe to run on the Glon thread.

---

## 6. Optional vocabularies and module architecture

**[P]** Keep vocabulary and implementation separate:

```text
s/find       semantic contract: string,string -> offset|no-match
s/find       portable Glon implementation      (always available)
s/find-fast  optional masm implementation       (same contract)
file/read    semantic contract: request/reply   (host-mediated)
file/read    portable host implementation       (per platform)
```

- The application depends on the **documented contract**, not on whether the
  implementation is Glon, masm, C or remote.
- A module selects its implementation explicitly at build/load time. There is
  no automatic polymorphism, implicit conversion or general plugin discovery.
- The minimal build exposes no optional vocabulary and links no optional
  service.
- A module may provide both a portable fallback and an accelerated binding only
  if both pass the same conformance suite.
- Namespaces (`s/`, `b/`, `file/`, `net/`, device) are vocabulary boundaries,
  not automatically a service registry. A namespace can contain ordinary Glon
  functions, masm words and host-mediated jobs with documented equivalence.
- Capability names should describe semantics (`file/app-read`,
  `file/app-write`, later `net/app-connect`) and remain separate from module
  presence and user grants.

**[I]** For `s/find`, the portable implementation can be the semantic oracle;
a masm implementation is the first acceleration candidate. For `file/`, the
semantic contract is inherently asynchronous and host-capability-gated; a
portable Glon implementation cannot create filesystem authority.

---

## 7. Resource and portability comparison

Legend: **[V]** measured/established; **[I]** inferred; numbers are IO-3
measurements where applicable.

| Dimension | A IO-3 | B extensible HOST | C generic dispatch | D masm/library |
|---|---|---|---|---|
| Seven opcodes preserved | [V] yes | [I] yes if new ids only | [I] yes if new id only | [V] yes |
| Existing HOST ids preserved | [V] yes | [I] append-only possible | [I] append-only possible | [V] yes |
| Closed HOST table preserved | [V] yes | [I] no | [I] no | [V] yes |
| Frozen source hashes preserved | [V] yes | [I] no | [I] no | [V] yes |
| Native call overhead | frame + source + worker | lowest | low + validation | S1 instruction loop |
| Data copies | frame/reply copies | pointer or explicit copy | pointer or explicit copy | none at ABI |
| Managed-heap ownership | copied managed strings | high risk if raw pointers | high risk if raw pointers | normal managed values |
| Loader lifetime | transient reply expressions | none | descriptor source only | library source |
| Determinism | external I/O dependent | bounded op deterministic | bounded op deterministic | deterministic |
| Capability model | existing grants | needs compile+runtime model | descriptor + grants | ordinary library |
| Async external I/O | supported | still needs event loop | still needs event loop | A / host-mediated |
| Linux/Windows/WASM/embedded | POSIX/pthread only today | requires per-host switch | requires per-host table | portable Glon, subject to host |
| Distributed use | framing reusable | local C only | semantic id reusable | library repeatable |
| Omit from minimal build | [V] yes | needs conditional source | needs static table filtering | [V] yes |
| Binary delta | [V] 231,872 bytes `-O0 -g` total | per-service code | one dispatch + tables | library code |

**[V]** IO-3 current executable size and buffers: 231,872 bytes `-O0 -g`, two
8,192-byte driver buffers, 400-byte chunks, default 8 handles, 8 MiB streaming
cap (`GLON-OPTIONAL-IO-IMPLEMENTATION.md`; `glon_io_file.h:18-29`).
**[I]** A native find service would add little code and remove copies, but
would not by itself reduce the file worker/framing cost.

---

## 8. Distributed execution implications

**[I]** A semantic service contract is useful across nodes; a transport is not
the contract.

- A local call can avoid serialization: validated arguments/result in-process.
- A remote call needs a byte-oriented, versioned frame, request ids, status
  codes, limits and cancellation. IO-3's v1 frame is already suitable for that
  role (`glon_io_file.h:18-29`).
- The same `s/find` or `file/read` contract can have a local adapter and a
  remote adapter. Identical transport is not required; identical observable
  status/result semantics is.
- Remote handles and permissions belong to the remote node; no local pointer
  or descriptor may cross. Object handles must remain opaque and
  generation-checked.
- Determinism is weaker remotely: network timing, remote filesystem state and
  node version must be explicit in status/version fields.
- Capability discovery must distinguish "service exists", "module loaded",
  "permission granted" and "resource available"; compilation or deployment is
  not authority.
- Framing is useful for distributed nodes; it is unnecessary overhead for a
  local bounded computation. IO-3 already demonstrates that local transport
  can be optimized separately from the wire contract.

---

## 9. Reference-operation traces

### 9.1 `s/find text pattern`

**A (current).** `stream/find-first` → `find-range` → `find-small` → repeated
`s/raw-at` comparisons (`modules/file-stream.glon:18-64`). No HOST service,
frame, worker or allocation per candidate. Cost is S1/Glon function dispatch:
about 246 s for a 1 MiB no-match stream. Return is an offset or `-1`; the
streaming contract rejects an empty pattern before starting.

**B.** Masm pushes managed string payload pointers and lengths plus bounds, then
`HOST_FIND`. C scans, validates lengths and returns an offset/status. No data
copy, no worker, no frame. The C function must not allocate or trigger GC, and
the host must reject out-of-range pointers/lengths. Ordinary Glon sees a
`func` wrapper over the masm call.

**C.** `HOST_SERVICE(find, v1, text descriptor, pattern descriptor, bounds)`.
The host validates the descriptor and service version, calls the same scanner,
and returns offset/status. One generic dispatch replaces a per-service id; no
frame or worker.

**D.** A masm implementation loops over bytes with existing instructions and
returns offset/no-match. It can reuse the same public `s/find` contract as a
plain Glon implementation. No C call, no GC/pointer risk, no frozen change.
This is the first experiment to run.

### 9.2 Asynchronous file read

**A.** Glon `file/read` opens a job, issues structured requests, the host
copies a v1 frame to host storage, a worker performs POSIX I/O, the driver
joins and checks the id/status, then `io/reply` reconstructs a transient Glon
turn. Transient loader blocks are reclaimed. A separate host-dispatched job
can run while the worker is pending. This is the current working baseline.

**B.** A blocking `HOST_READ` would block the Glon execution thread and is
rejected. `HOST_IO_SUBMIT`/`POLL` could avoid local frame construction but still
needs a driver loop, pending-job state, handle ownership, permission checks and
cleanup. The complexity moves, not disappears.

**C.** A generic async service class could standardize submit/poll and status
mapping, but still needs an event loop and blocked-job protocol. Local
transport could be direct; remote transport could use the same semantic id and
a framed adapter.

**D.** No synchronous C file operation is reachable above S1. The
host-mediated asynchronous driver remains. A masm library cannot create host
authority or filesystem resources.

---

## 10. Frozen-S1 implications, distinguished

| Requirement | A | B | C | D |
|---|---|---|---|---|
| Keep seven opcodes | yes | yes (new HOST id only) | yes (new HOST id only) | yes |
| Keep existing instruction semantics | yes | yes for old ids | yes for old ids | yes |
| Keep existing HOST behaviour | yes | append-only IDs can preserve old effects | append-only generic id can preserve old effects | yes |
| Keep HOST table closed | yes | **no** | **no** | yes |
| Preserve ordinary binary compatibility | yes | old programs yes; new programs need new machines | old programs yes; new programs need new machines | yes |
| Preserve frozen source hashes | yes | **no** unless a new machine/tag is created | **no** unless a new machine/tag is created | yes |
| Avoid arbitrary C pointers | yes | must be enforced by static dispatch/validation | must be enforced by descriptor validation | yes (no C) |

**[V]** `check-frozen-s1.sh` will fail for any edit to `s1.c`/`s1.h`; the
tag/hash contract is not the same as "seven opcodes". A new service table must
therefore be a new version/tag (S2 or equivalent), or must stay outside S1
(IO-3 / masm). It cannot be called "S1 unchanged" merely because it still has
seven opcodes.

---

## 11. Recommended focused experiments

These are proposals only; none changes the interpreter in this task.

1. **Masm `s/find`** — implement a bounded, no-allocation search in masm using
   the existing instruction set; benchmark 1 MiB and 4 MiB no-match streaming
   against 246 s/MiB. If it removes the dominant cost, keep HOST closed.
2. **Masm byte operations** — compare/checksum/scan microbenchmarks to see
   whether the same approach covers the bounded-computational class.
3. **IO-3 overhead breakdown** — instrument native extraction, frame copy,
   worker creation/join, reply source construction and callback time separately.
   Quantify how much is framing versus worker/loader handling before
   considering any transport change.
4. **Local-vs-distributed contract** — paper prototype a versioned service
   descriptor with sync/async classes, status mapping and capability grants;
   test the same `s/find` contract through a local adapter and a framed remote
   adapter without changing S1.
5. **Capability catalogue** — extend the existing semantic capability model to
   optional computational services and test absent/denied/unavailable behavior.
6. **New-machine paper design (only if 1–5 show a real gap)** — generic
   `HOST_SERVICE` or equivalent, append-only, static descriptors, no pointers,
   explicit invalid/unavailable status, minimal-build omission and verifier
   arity rules; treat it as a separate S2 contract.

---

## 12. Recommendation on the closed HOST table

**Keep the closed HOST service table frozen at `s1-frozen-v1` for the current
phase.**

Reasons:

1. **[V]** External file I/O complexity is dominated by asynchronous
   completion, worker/event handling, capability policy and reply lifetime,
   not by the lack of a HOST id. Extending HOST would not remove those.
2. **[V]** The 246 s/MiB no-match cost is in Glon search, not in HOST
   dispatch. A masm experiment can attack it without any frozen change.
3. **[I]** The closed table gives a finite, portable host contract and keeps
   platform-specific facilities out of the substrate. The seven opcodes are
   not the same thing as the table, but the table is still a deliberate
   contract whose source hashes are frozen.
4. **[I]** Any extension needs invalid-id, capability, GC/pointer, verifier,
   binary-version and per-host implementation decisions. That is a new machine
   contract, not an incidental optimisation.

If future measurements show that bounded native services are necessary and
masm is insufficient, the preferred path is a **new, explicitly versioned
generic service contract** (new machine/tag or an out-of-band host module),
with static descriptors, separate sync/async classes, strict validation and a
minimal-build stub. Per-service file/network HOST ids should not be added to
S1.

**IO-3 remains the working baseline for external I/O.** This investigation
does not authorise an implementation change and does not modify frozen S1.

---

## 13. Source index

- S1 instruction/service contract: `s1.h:30-49`; `s1.c:42-55,132-163,173-191`.
- Frozen decisions and boundaries: `GLON-ARCHITECTURE-MEMORY.md:66-82,798-808`;
  `MEMORY.md:35-46`; `DESIGN.md` (Substrate Principle);
  `R0-ARCHITECTURE.md:162-189,662-687`.
- Native trampoline: `r0_s1.h:53-71`; `r0_s1_runtime.c:3089-3258`.
- Masm validation: `r0_s1_runtime.c:462-535`; `MACHINE-INTEGRITY-AUDIT.md:139-143`.
- WASM host ABI: `GLON-WASM-ABI.md:49-52,88-96,518-527`.
- IO-3 request/reply: `glon.c:243-329,441-503`;
  `glon_io_file.h:18-29`; `glon_io_file_cli.c:41-67`;
  `modules/file.glon:61-134`; `modules/file-stream.glon:126,166`.
- Streaming measurements: `GLON-OPTIONAL-IO-STREAMING.md:38,194`.
- Host capability model: `GLON-HOST-CAPABILITIES.md` (control/data planes,
  capabilities versus permissions, authority boundary).
