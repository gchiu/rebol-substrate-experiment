# Distributed Glon — federated agents over a shared coordination space

Design direction (not yet implemented). This records the emerging distributed
model so it is preserved accurately before any implementation begins.

## Motivation

The same Glon philosophy that permits several computational models above one
frozen local substrate can also permit many Glon instances to cooperate above an
abstract distributed coordination space.

The original inspiration was the observation that independent AI agents, when
needing to cooperate, can spontaneously create something resembling a
bulletin-board system. A BBS supplies persistence, visibility and asynchronous
communication — but it is a human-shaped workaround.

A deliberate Glon system should move beyond textual BBS communication toward
structured, machine-readable coordination:

```text
BBS
 ↓
structured blackboard
 ↓
tuple space
 ↓
distributed Glon space
```

The BBS may remain useful as a human-readable projection or activity log, but it
should not be the machine protocol.

## Core shared-space model

Candidate minimal operations:

```text
put value
take pattern
read pattern
watch pattern
```

- `put value` — publish a structured item into the space.
- `take pattern` — atomically claim/remove a matching item, optionally
  suspending until one becomes available.
- `read pattern` — observe a matching item without consuming it.
- `watch pattern` — react when matching items appear/change.

```text
put [
    task      translate
    language  cantonese
    id        3817
    text      "..."
]
```

A worker might:

```text
job: take [
    task      translate
    language  cantonese
    id        ?
    text      ?
]
```

and later:

```text
put [
    result-for  job/id
    value       "..."
]
```

The producer does not need to know which worker will perform the task.

## Local and distributed execution share semantics

The same coordination model should scale through:

```text
green tasks in one Glon
        ↓
multiple Glon instances on one machine
        ↓
multiple machines
        ↓
multiple Jetsons
        ↓
federated Jetson network
```

A program should not need to know whether a compatible worker is another green
task, another WASM instance, another process, another CPU/GPU, another Jetson,
another machine or another rack. HOST/transport supplies that distinction; the
programming model stays stable.

## Transport independence

Distributed Glon must not depend on a specific physical/network technology.
Possible transports: in-memory queues, shared memory, Unix/local IPC, Ethernet,
Wi-Fi, fibre, message brokers, WebSockets, QUIC/TCP, future high-speed fabrics,
and UnifiedBus/UALink/NVLink-like systems where available.

```text
Glon distributed semantics
        ↓
transport abstraction
        ↓
physical/network fabric
```

No proprietary fabric (e.g. Huawei UnifiedBus) becomes part of Glon semantics. A
faster fabric improves implementation performance without changing Glon programs.

## Federated Jetson agents

A network of Jetsons can host independent Glon agents that cooperate through the
shared space. This is **not necessarily a master/worker system**: each Jetson may
advertise capabilities, claim work appropriate to those capabilities, publish
results, disappear/reappear, and retain independent ownership and permissions.

```text
Jetson A ─┐
Jetson B ─┤
Jetson C ─┼── distributed Glon space
Jetson D ─┤
Jetson E ─┘
```

```text
offer [
    agent        jetson-3
    capabilities [
        ocr
        vision
        embedding
        translate-cantonese
    ]
]
```

Tasks are matched to capability rather than addressed to a named machine,
allowing collaboration without a rigid central controller.

## Contracts are mandatory

A tuple/shared space without contracts permits communication, but not reliable
cooperation or governance. Every significant shared-space protocol should have
an explicit contract describing three dimensions.

### Data contract

Request/result shape and types:

```text
contract Translate [
    request [
        id       integer!
        text     string!
        from     language!
        to       language!
    ]

    result [
        request-id integer!
        text       string!
    ]

    failure [
        request-id integer!
        reason     word!
    ]
]
```

### Behavioural contract

Observable semantics: what constitutes successful completion, permitted failure
results, idempotency, retry behaviour, deadline, lease duration, result lifetime,
and ordering guarantees where required.

```text
contract OCR [
    lease       30s
    retries     3
    idempotent  yes
    result-ttl  10m
]
```

### Capability / security contract

Who may publish a request, claim it, inspect it or publish results; which
resources/data may be accessed; and what authority the worker receives. Agents
receive the least capability required to fulfil a contract.

## Contracts define boundaries between computational models

An FBP component, reactive subsystem, actor, ordinary Glon function, remote
Jetson agent or future AI model need not use the same internal computational
model — they need only agree on the contract at the boundary.

```text
FBP component
      │
   contract
      │
tuple space
      │
   contract
      │
Jetson agent
```

This supports the larger principle: **choose the computational model appropriate
to the problem; use contracts to make different models composable.**

## Leases and failure recovery

Distributed workers can fail after claiming work, so `take` should normally
support claim/lease semantics:

```text
worker claims task
      ↓
lease = 30 seconds
      ↓
worker completes
      ├── publish result + acknowledge
      │
      └── or disappears
                 ↓
            lease expires
                 ↓
          task becomes available
```

This tolerates worker failure, network partition, machine reboot, overload and
temporary disappearance without permanently losing work. Exactly-once execution
should not be assumed unless explicitly supported by the contract; prefer
contract-defined idempotency and at-least-once recovery.

## Structured messages, not prose

Machine coordination uses structured values (`task`, `contract`, `capability`,
`payload`, `identity`, `deadline`, `lease`, `correlation-id`, `result`,
`failure`). Agents must not be required to write prose, scan prose, interpret
intent, or decide whether a message applies. A human UI may render these values
into prose/log entries.

## Distributed inference (separate, future)

Recorded carefully as a future experiment, not a demonstrated capability.

- **Distributed independent inference**: different Jetsons run complete or
  specialist models and receive independent jobs through the space — a natural
  fit for the tuple-space architecture.
- **One inference distributed across Jetsons**: a larger model partitioned into
  pipeline stages (`layers 0–7 → 8–15 → 16–23 → 24–31`) — technically distinct
  and far more sensitive to interconnect bandwidth/latency.

The shared coordination layer could potentially orchestrate such stages while
HOST/runtime-specific code handles tensor transfer. This is not implemented.

## Security principle

Distributed agents must not imply unrestricted mutual trust. Each participant
has explicit identity/capability boundaries; a Jetson may expose some
capabilities while retaining private state:

```text
public      [weather lookup]
household   [shopping tasks]
trusted     [document OCR]
private     [mail financial-data credentials]
```

The principle is capability-limited federation, not a universal hive mind.

## Non-goals at this stage

Do not yet implement: network primitives in S1, distributed semantics in the
evaluator, a message broker, consensus algorithms, distributed shared memory,
cluster membership, distributed inference, or Jetson-specific runtime code.
This is an architectural design direction preserved for later work.
