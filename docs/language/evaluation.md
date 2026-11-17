# Glon Evaluation Model

This document explains how an implemented Glon program executes: how words
resolve, how values flow, how calls consume arguments, how state changes, and
where control unwinds. It is descriptive and subordinate to
`GLON-ALPHA-LAWS.md`.

The evaluator is emitted S1 code (`r0_s1_runtime.c`); this document describes
that code, not a separate C interpreter. The frozen S1 substrate only executes
seven primitives (`LIT DUP DROP @ ! 0BRANCH HOST`).

---

## 1. Layers and responsibility

```mermaid
flowchart TD
    A["Glon source: words, blocks, closures, SIN!"] --> B["Reader and loader: r0_s1_parse"]
    B --> C["Evaluator emitted as S1: subexpr, native, closure, return"]
    C --> D["R0 runtime mechanisms: contexts, GC, loader, tasks"]
    D --> E["Frozen S1: seven primitives, memory-mapped registers"]
    E --> F["Host: native CLI, WASM, browser, Jupyter"]
    G["Libraries: prelude, case, parse, strings, tasks"] --> A
```

Language semantics live in the evaluator and the laws. Hosts move bytes and
present output; they do not implement evaluation. Library words are ordinary
Glon built from the core mechanisms.

---

## 2. Runtime state

The machine is one persistent, single-threaded 65,536-cell memory
configuration. The important logical state is:

| State | What it holds |
|---|---|
| Current context (`RV_CTX`) | The lexical environment used by word lookup |
| Child context (`RV_CHILD`) | The context of the activation being invoked |
| Current closure (`RV_CLOSURE`) | The closure being applied |
| Activation frame chain (`RV_FRAME`) | Linked frames for return and judges |
| Current block/cursor/end (`RV_BLK`, `RV_CUR`, `RV_END`) | The block being evaluated |
| Data stack | Tagged values and result sets |
| Return stack | Continuations, activation frames, stack-local contexts |
| Global context | 256-binding root environment |
| Managed heap | GC-managed blocks, strings, closures, contexts, SIN! |
| Loader heap | Permanent parsed source and symbols |
| Task table | Three cooperative task records |

```mermaid
flowchart TD
    G["Global context: 256 bindings"] --> C1["Activation context A: 16 bindings"]
    C1 --> C2["Activation context B: 16 bindings"]
    C2 --> C3["Closure-captured context"]
    F["Frame chain"] --> F1["frame: site, SP, RP, IP, CTX, CUR, END, BLK, bias"]
    F1 --> F2["previous frame"]
    DS["Data stack"] --> DV["result sets and temporaries"]
    RS["Return stack"] --> RV["continuations, frames, stack contexts"]
    MH["Managed heap"] --> MO["blocks, strings, closures, contexts, SIN!"]
    LH["Loader heap"] --> LO["source blocks and interned symbols"]
```

Contexts are a parent chain. Lookup walks from the current context toward the
global root. Frames are a separate chain used for return, judges, context
origin and the Guard of Binding.

---

## 3. Running a program

### 3.1 Host entry

A typical run:

```mermaid
sequenceDiagram
    participant H as Host
    participant R as r0_s1_run
    participant E as Evaluator
    participant G as Global context
    H->>R: block value
    R->>R: set CUR END BLK CTX global, clear frames
    R->>R: reset stacks and S1 registers
    R->>E: run main entry
    E->>E: evaluate every form in sequence
    E->>G: lookups and assignments as needed
    E-->>R: final result set
    R->>R: check stack sentry
    R-->>H: N results
```

`r0_s1_run` is fresh-state; `r0_s1_run_persistent` preserves the managed heap
frontier so definitions survive across calls. Both reset the transient data
stack, return stack, frame chain and current context to the global context at
the start of each run (`r0_s1_runtime.c:4103-4158`).

### 3.2 Block sequence and result sets

Evaluating a block:

```mermaid
flowchart TD
    A["start: CUR at first form, END after last"] --> B{"CUR before END?"}
    B -->|"no"| Z["return NONE as one result"]
    B -->|"yes"| C["evaluate one subexpression"]
    C --> D{"more forms?"}
    D -->|"yes"| E["discard this result set"]
    E --> B
    D -->|"no"| F["return this result set"]
```

Each subexpression yields **zero or more results**. All but the last are
discarded by the block evaluator. The last result set is the block's result.
This is why `[1 2]` yields 2, while `[values [1 2]]` yields two results.

### 3.3 Result-set rules at call sites

Most operations collapse an argument expression to one value:

```mermaid
flowchart TD
    A["argument expression"] --> B["evaluate subexpression"]
    B --> C{"number of results N"}
    C -->|"N = 0"| D["argument value is NONE"]
    C -->|"N = 1"| E["argument value is that value"]
    C -->|"N > 1"| F["argument value is the first result"]
```

Consequences:

```glon
+ values [1 2] 3      ;; => 4  (first result 1, then 3)
reduce [values [1 2] 3] ;; => [1 3]
```

This collapse is IMPLEMENTED and is not stated in the Alpha laws. `values` is
the exception that preserves all results, but it requires each source form to
yield exactly one result.

---

## 4. Subexpression dispatch

Every form goes through `emit_subexpr` (`r0_s1_runtime.c:3569-3745`).

```mermaid
flowchart TD
    A["read value at CUR; CUR++"] --> T{"tag"}
    T -->|"INT NONE BLOCK CONTEXT STRING SIN USER"| S["self-evaluate as a value"]
    T -->|"WORD"| W["dynamic lookup"]
    T -->|"BOUND"| B["direct lexical slot read"]
    T -->|"SET-WORD"| SW["evaluate one expression; assign"]
    T -->|"GET-WORD"| GW["lookup; do not invoke"]
    T -->|"LIT-WORD"| LW["return plain WORD"]
    T -->|"CLOSURE"| CL["invoke closure with arity arguments"]
    T -->|"NATIVE"| NA["invoke native with arity arguments"]
    T -->|"RAW"| RA["invoke RAW with arity arguments"]
    W --> V{"value callable?"}
    B --> V
    V -->|"closure native RAW"| I["invoke"]
    V -->|"anything else"| Y["yield value"]
    S --> Y
    GW --> Y
    LW --> Y
    SW --> Y
    CL --> Y
    NA --> Y
    RA --> Y
    I --> Y
```

`func` and `return` are detected before ordinary WORD lookup, because they are
reserved keywords in value position. `masm`/`raw` are resolved by the reader,
not the evaluator.

---

## 5. Word resolution

### 5.1 Lookup

```mermaid
flowchart TD
    A["WORD symbol id"] --> B["start at RV_CTX"]
    B --> C{"context is NONE?"}
    C -->|"yes"| H["machine fail-stop: unbound word"]
    C -->|"no"| D{"binding with this symbol id?"}
    D -->|"yes"| E["return binding value"]
    D -->|"no"| F["move to parent context"]
    F --> C
```

Lookup is dynamic through the lexical parent chain. It is not re-lexicalised.
A pre-resolved BOUND value skips the name search and reads `(depth, slot)`
directly, guarded by the frame site (`emit_load_lex`,
`r0_s1_runtime.c:2228-2291`).

### 5.2 Assignment

```mermaid
flowchart TD
    A["SET-WORD name and RHS value"] --> B["start at RV_CTX"]
    B --> C{"context is NONE?"}
    C -->|"yes"| D{"current context has room?"}
    D -->|"no"| X["CONTEXT_FULL fail-stop before writing"]
    D -->|"yes"| E["append name to current context"]
    C -->|"no"| F{"binding found?"}
    F -->|"yes"| G["escape-law store check"]
    G --> H["write value into existing slot"]
    F -->|"no"| I["move to parent context"]
    I --> C
```

`r_set` (`r0_s1_runtime.c:2578-2643`) performs nearest-update. A local is
created only when the name exists nowhere in the chain. The `CONTEXT_FULL`
check occurs before any mutation.

### 5.3 Parameters and locals

On a closure call:

1. The caller evaluates all argument expressions left-to-right.
2. The evaluator creates a new child context under the closure's captured
   context.
3. It binds the spec's parameter words, in reverse spec order, into that
   context.
4. A set-word inside the body updates a parameter or an existing outer binding
   if present, otherwise it appends a local to the activation's 16-binding
   context.

The reverse-order bind is an implementation layout detail; programs see the
spec order.

---

## 6. Calls

### 6.1 Native and closure calls

```mermaid
sequenceDiagram
    participant Caller
    participant Eval as Evaluator
    participant Callee as Native or Closure
    Caller->>Eval: reach callable word or value
    Eval->>Eval: read arity
    loop each argument
        Eval->>Eval: evaluate one subexpression
        Eval->>Eval: collapse to one value
    end
    Eval->>Callee: apply with argument values
    Callee-->>Eval: result set
    Eval-->>Caller: result set
```

A plain word bound to a closure auto-invokes. `:f` retrieves the closure
without invoking it; `invoke :f` invokes it. `invoke` evaluates exactly one
expression to obtain a closure, then consumes the closure's arity from the
same caller expression stream.

### 6.2 Closure activation

```mermaid
sequenceDiagram
    participant Caller
    participant Eval as Evaluator
    participant Frame as Frame and child context
    participant Body as Body block
    Caller->>Eval: callable closure plus arguments
    Eval->>Eval: evaluate arguments
    Eval->>Frame: allocate child context under captured context
    Eval->>Frame: bind parameters
    Frame->>Frame: allocate activation frame on return stack
    Eval->>Body: evaluate body in child context
    Body-->>Eval: result set
    Eval->>Frame: restore caller CUR END BLK CTX
    Eval->>Eval: release frame
    Eval-->>Caller: result set
```

A normal closure return does not use `return`; it reaches the end of the body,
restores saved caller state from the frame, and continues after the call.
`return` is a separate non-local path (§7).

### 6.3 Capture and promotion

```mermaid
flowchart TD
    A["func creates a closure"] --> B["capture current context"]
    B --> C{"captured context is stack-local?"}
    C -->|"no"| D["store captured managed or loader context"]
    C -->|"yes"| E["check escape law: foreign dependent blocks?"]
    E -->|"violation"| X["raise escape/capture"]
    E -->|"ok"| F["promote context to managed heap"]
    F --> G["redirect owning activation to managed copy"]
    G --> D
```

A closure always captures a context identity. Promotion means the activation
and its closures share one context, so mutation through either is visible to
both. A stack context that never escapes is reclaimed with its activation.

---

## 7. Non-local return

`return` reads the current block's site id, evaluates one expression, finds the
nearest live activation with that site, restores that activation's saved state
and jumps to its return address.

```mermaid
sequenceDiagram
    participant Body as Deep body
    participant Helper as Unaware helper
    participant Target as Target activation
    participant Caller as Caller
    Caller->>Target: call f
    Target->>Helper: call helper
    Helper->>Body: do block containing return
    Body->>Body: evaluate return argument
    Body->>Target: find frame with block site
    Target->>Target: restore SP RP IP CTX CUR END BLK
    Target-->>Caller: result set
    Note over Helper: helper epilogue never runs
```

- `return` outside a function, or with no matching frame, is a machine
  fail-stop.
- A judge frame copies the enclosing site but is not an activation; `return`
  skips it and returns from the real function.
- A CASE action is a zero-argument closure, so `return` inside it returns from
  the action and CASE yields the value.

---

## 8. `do`, `values`, `reduce`, `either`

These operations differ in which block is executed and how results are kept.

```mermaid
flowchart TD
    A["do block"] --> B["run in current context and activation"]
    B --> C["return final result set"]
    D["either cond then else"] --> E["evaluate cond once"]
    E --> F["run exactly one branch block"]
    F --> C
    G["values block"] --> H["run each form in sequence"]
    H --> I["require exactly one result per form"]
    I --> J["return all results"]
    K["reduce block"] --> L["run each form in sequence"]
    L --> M["collapse each form to first result"]
    M --> N["allocate managed block of values"]
    N --> O["return one block"]
```

| Operation | Executes block? | Context | Result |
|---|---|---|---|
| `do` | yes | current | final result set |
| `either` | one branch | current | branch result set |
| `values` | yes, all forms | current | all results, one per form |
| `reduce` | yes, all forms | current | one managed block |
| `judge` | yes | current | result set or SIN! |
| bare block | no | — | the block value |

A managed block produced by `reduce` is data, not code. Executing it with
`do`, `values`, `reduce` or `judge` is a machine fail-stop.

---

## 9. SIN! propagation and `judge`

A SIN! is a value. `raise` is control flow.

```mermaid
sequenceDiagram
    participant Caller
    participant Judge as judge frame
    participant Deep as Deep operation
    participant Unwind as Raise unwind
    Caller->>Judge: judge block
    Judge->>Judge: push transparent frame copying site and bias
    Judge->>Deep: run block in place
    Deep->>Deep: raise err
    Deep->>Unwind: set pending SIN and unwind
    Unwind->>Unwind: walk frame chain for nearest judge in this task
    Unwind->>Judge: restore judge caller state
    Judge-->>Caller: return SIN value
    Note over Caller: execution continues after judge
```

- The judge frame copies the enclosing activation's site and bias, so lexical
  references inside the judged block resolve as if the judge were not there.
- `return` inside a judged block unwinds through the judge to the enclosing
  function.
- A raise with no judge ends the run as an uncaught SIN!.
- Judges are per task. A task raise is caught only by a judge in that task.
- Machine-integrity failures are not SIN!s; no judge catches them.

---

## 10. Escape-time binding law

A block that depends on an activation may be used while that activation is
live, but may not escape it.

```mermaid
flowchart TD
    A["activation-dependent block"] --> B{"boundary"}
    B -->|"frame exit"| C["raise escape/return"]
    B -->|"closure argument same site"| D["raise escape/argument"]
    B -->|"set-word into foreign context"| E["raise escape/store"]
    B -->|"context capture or promotion"| F["raise escape/capture"]
    B -->|"task body transport"| G["raise escape/transport"]
    B -->|"used while origin live"| H["legal"]
```

The check happens at the boundary, before the illegal value is stored,
returned, captured or transported. A judge receives the SIN!, never the
illegal block. A closure is the portable executable form; a bare bound block
is not.

---

## 11. Tasks

Tasks are cooperative and deterministic. There is no pre-emption and no
parallel execution.

```mermaid
sequenceDiagram
    participant Main
    participant Sched as Scheduler record
    participant T1 as Task 1
    participant T2 as Task 2
    Main->>Main: spawn body
    Main->>Sched: create task record
    Main->>Sched: run-tasks saves main state
    Sched->>T1: restore task 1
    T1->>Sched: yield saves task 1
    Sched->>T2: restore task 2
    T2->>Sched: yield saves task 2
    Sched->>T1: round-robin
    T1->>Sched: finish marks task 1 done
    Sched->>T2: resume task 2
    T2->>Sched: finish
    Sched-->>Main: NONE when no runnable task remains
```

Each task record saves IP, SP, RP, CUR, END, CTX, BLK, FRAME and state. A task
runs only until `yield` or completion. A body that depends on the spawning
activation is rejected before the task table changes.

---

## 12. Worked traces

### 12.1 `+ 1 * 2 3`

```mermaid
flowchart TD
    A["evaluate +"] --> B["native arity 2"]
    B --> C["arg 1: INT 1"]
    B --> D["arg 2: evaluate *"]
    D --> E["native arity 2"]
    E --> F["arg 1: INT 2"]
    E --> G["arg 2: INT 3"]
    E --> H["2 * 3 = 6"]
    C --> I["1 + 6 = 7"]
    H --> I
    I --> J["result 7"]
```

### 12.2 Factory closure

```glon
make-adder: func [n] [ func [x] [ + x n ] ]
add10: make-adder 10
add10 5
;; => 15
```

1. `make-adder` is called with `n = 10`; its child context holds `n`.
2. The body evaluates `func [x] [ + x n ]`, a literal body. The inner closure
   captures the `make-adder` activation's context and has bias 0.
3. The activation returns the closure. Its context is promoted to the managed
   heap because the closure escapes.
4. `add10 5` creates a new context for `x`, parented to the captured context.
5. `+ x n` reads `x` from the current context and `n` from the captured parent.

### 12.3 Mutable counter

```glon
counter: func [] [ n: 0  func [] [ n: + n 1 ] ]
c: counter
c c c
;; => 3
```

- `n: 0` has no existing binding, so it creates a local in the `counter`
  activation.
- The inner closure captures that context; it is promoted on return.
- Each call to `c` finds the existing `n` in the captured parent and updates it.
- This is one shared captured context, not a copy per call.

### 12.4 Non-local return through unaware code

```glon
helper: func [b] [ do b  99 ]
outer:  func [] [ helper [ return 42 ]  0 ]
outer
;; => 42
```

`return` inside the block sees the block's site (`outer`), evaluates 42, walks
the frame chain to `outer`, restores its state and returns. `helper` never
executes its `99` or its epilogue.

### 12.5 Judge

```glon
x: 1
e: judge [ raise create-sin 'demo 'oops 7 ]
x: + x 1
x
;; => 2
```

`judge` runs the block. `raise` unwinds to the judge frame, which returns the
SIN! as a value. The assignment after the judge runs normally.

---

## 13. State-change summary

| Operation | Affects |
|---|---|
| `name: value` | nearest binding, else current context |
| Parameter bind | new activation context |
| Closure creation | captures a context; may promote it |
| `do` | no new context; executes in current |
| `either` | no new context; executes one branch block |
| `judge` | adds a transparent frame; catches raises |
| Function call | new context, frame, return-stack reservation |
| `return` | unwinds to the matching function frame |
| `raise` | unwinds to the nearest judge in the same task |
| `reduce` | allocates one managed block |
| `values` | does not allocate a result block itself |
| `s/+` etc. | allocate a new managed string |
| `spawn` | creates one task record if the body passes the transport check |
| GC | reclaims unreachable managed objects; loader heap is permanent |
