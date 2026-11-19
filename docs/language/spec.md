# Glon Language Specification

**Status:** descriptive specification of Glon Alpha as implemented in this
repository. It records the language contract and the observed runtime; it does
not create new language behaviour.

This document is **subordinate** to:

1. **`GLON-ALPHA-LAWS.md`** — the normative Alpha language contract. Where
   this document and the laws differ, the laws win.
2. **The implementation** — `r0_s1_runtime.c`, `r0_s1.h`, `m1_layout.h`, and
   the frozen S1 substrate (`s1.c`, `s1.h`, `tests.c`, `adversarial.c`,
   `claims.c`, tag `s1-frozen-v1`, commit `f90496c…`). The code is the ground
   truth for addresses, capacities and any behaviour the laws do not name.

The language is frozen at Alpha (`glon-alpha-v1`). The frozen S1 substrate is
verified by `./check-frozen-s1.sh`.

## How to read this specification

Every statement that describes behaviour is labelled with one of:

| Label | Meaning |
|---|---|
| **FROZEN** | Guaranteed by an existing contract (`GLON-ALPHA-LAWS.md` or the frozen S1 contract). |
| **IMPLEMENTED** | Observed in the implementation but not guaranteed by a frozen contract. |
| **EXPERIMENTAL** | Present, but explicitly subject to change or not part of ordinary sessions. |
| **UNRESOLVED** | Insufficient evidence, unchecked behaviour, or conflicting descriptions. It is not a promise. |

An **unqualified** statement in a normative-looking section is FROZEN if it
restates a law; otherwise it is IMPLEMENTED. Contradictions are recorded in
§11 rather than silently resolved.

Examples use `;;` comments. The Glon reader itself has no comment syntax:
`;;` line comments are stripped by the **host** before parsing (see §1.1).

---

## 1. Source language

### 1.1 Source envelope and comments

**FROZEN host contract; IMPLEMENTED parser detail.**

- A program is a sequence of values. The canonical source form is one outer
  block:

  ```glon
  [
      x: 5
      + x 3
  ]
  ```

- The native session host, the WASM `glon_load`/`glon_run` path, the Jupyter
  host and the native `glon` CLI wrap an unwrapped cell as `[ src ]`. The
  parser itself also accepts a top-level sequence of forms without the outer
  brackets (`parse_program`, `r0_s1_runtime.c:3963`).
- **`;;` line comments are not parser syntax.** Hosts strip from `;;` to the
  end of the line before parsing (`r0_s1_session.c:73`, `r0_s1_show.c:110`,
  `standalone/glon.c:305`, `glon.c:62`). A single `;` is an ordinary token
  character.
- The comment stripper is lexical and **not string-aware**. `;;` inside a
  quoted string is also stripped. This is an IMPLEMENTED host limitation, not
  a language comment rule.
- If a source begins with `[`, the parser parses that one outer block and
  ignores any forms after it. This is an IMPLEMENTED parser quirk; canonical
  hosts do not rely on trailing forms.

### 1.2 Lexical grammar

**IMPLEMENTED (reader at `r0_s1_runtime.c:177-216`, `643-661`, `764-886`,
`3963-4002`).**

Whitespace is the C `isspace` set: space, tab, newline, carriage return,
vertical tab and form feed. The only structural delimiters are `[` and `]`.
There are no parentheses, commas, semicolons, escapes, floats or character
literals in the Glon reader.

```ebnf
source         = ws* ( block trailing-tail | top-item ( ws* top-item )* ws* ) ;
trailing-tail  = any-byte* ;              (* ignored after a leading block *)
top-item       = string | masm-form | form ;
block          = "[" ( ws | block-item )* "]" ;
block-item     = string | func-form | masm-form | form ;
form           = block | integer | word ;
func-form      = "func" form block? ;     (* body may be left to the next item *)
masm-form      = ( "masm" | "raw" ) ( ws+ digit+ )? ws* masm-block ;
masm-block     = "[" masm-tokens "]" ;
string         = '"' non-quote-byte* '"' ;          (* item-start only; 0..512 bytes *)
integer        = [ "-" ] digit+ ;
word           = token ;
word-value     = "none" | token ":" | ":" token | "'" token | token ;
token          = token-byte+ ;            (* reader keeps the first 63 bytes *)
token-byte     = ? any byte except whitespace, "[" or "]" ? ;
non-quote-byte = ? any byte except double quote ? ;
any-byte       = ? any byte 0..255 ? ;
masm-tokens    = ? private S1 macro-assembly tokens ? ;
digit          = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
ws             = " " | "\t" | "\n" | "\r" | "\v" | "\f" ;
```

This grammar names the two levels that the reader actually has:
`block-item`/`top-item` are what the block and program loops recognise
(strings, `masm`/`raw`, and inside a block `func`); `form` is what
`parse_form` recognises (block, integer or word only). A string at a `func`
spec/body expression position is therefore read as a word token, not as a
string literal. Keyword alternatives apply only when the whole maximal token
is exactly `func`, `masm` or `raw`; `funcx` is a word. Top-level `func` is not
special-cased by `parse_program`, so canonical hosts wrap source in one block.
The grammar is lexical only: token scanning is maximal, so `a"b"` is one word
while `1"x"` is an integer followed by a string. See
`GLON-SYNTAX-DIAGRAMS.md` §11 for the consolidated note.

Important lexical facts:

- A token is read up to the next whitespace, `[` or `]`, then **truncated to
  63 bytes** (`char buf[64]`, `r0_s1_runtime.c:194-198`). Two long tokens that
  share their first 63 bytes therefore intern to the same symbol.
- Forms need no whitespace when the next form starts with a delimiter:
  `a[1]` is a word then a block; `1"x"` is an integer then a string. A quote
  not at an item start is part of a word token (`a"b"` is one word).
- `func` is special-cased only while parsing a **block**. An unwrapped
  top-level `func` is an ordinary word to `parse_program`; canonical hosts
  wrap the source in one block.
- Symbols are case-sensitive and interned in a fixed table of 512
  (`r0_s1_runtime.c:22`, `56-64`). Exhaustion is a parse failure
  (`symbol_table_full`), not a runtime error.
- Integer literals are decimal with an optional leading `-`. A leading `+` is
  not accepted as a number; it is a word. `-` followed by a non-digit is a
  word. There is no overflow check.
- `none` is special: the exact token `none` is the NONE value. `none:`, `:none`
  and `'none` are word forms with the symbol `none`. Rebinding `none:` does not
  change the plain token `none`.
- There are no escape sequences in strings. `"` ends the string. A backslash
  is an ordinary byte. An unterminated string is a syntax error.
- A string literal is **expanded by the reader into two forms**,
  `mk-string [b0 b1 …]`, where each byte is a tagged integer 0–255. Therefore:
  - the literal requires a `mk-string` binding at run time;
  - it costs two forms against the 512-form block limit;
  - it is limited to 512 bytes.
- The `func` and `masm`/`raw` forms are recognised lexically in item position
  (inside a block for `func`; inside a block or at top level for `masm`/`raw`).
  `func` is a reserved keyword in value position (symbol id 0). `return` is
  reserved in value position (symbol id 1). `raw`/`masm` are parse-time forms;
  `raw` is also a reserved symbol (id 2) used by the compatibility alias.

### 1.3 Word forms

**FROZEN syntax; IMPLEMENTED evaluation.**

After a token is read, the first matching rule wins:

| Source token | Kind | Parsed value | Evaluates to |
|---|---|---|---|
| `none` | none literal | NONE | itself |
| `name:` | set-word | SET-WORD `name` | RHS value; assigns |
| `:name` | get-word | GET-WORD `name` | binding of `name`; never invokes |
| `'name` | lit-word | LIT-WORD `name` | plain WORD `name` (data) |
| `name` | word | WORD or BOUND `name` | lookup; if closure/native/RAW, invokes |

Consequences:

- The prefix `:` and `'` are part of the reader only. Their evaluated results
  are values, not syntax.
- **Evaluating a lit-word yields a WORD, not a LIT-WORD.** LIT-WORD cells can
  exist as unevaluated block elements and print as `'name`; evaluation removes
  the literal marker (`r0_s1_runtime.c:3735-3741`).
- `x:` returns the value assigned. `:x` returns the value without invoking it.
- A trailing `:` wins over a leading `:` or `'`. For example `'x:` is a
  set-word whose symbol is `'x`.
- An interior `/` is part of the token. `s/+`, `s/=`, `s/length` are ordinary
  flat words. A bare `/` is integer division. There is no path evaluation,
  namespace or qualified lookup (§9).

### 1.4 Blocks

**FROZEN.**

- `[ ... ]` builds a loader block. Its operational layout is
  `[count, site_id, elem0, elem1, ...]` (`BLK_COUNT`, `BLK_SITE`, `BLK_DATA`,
  `r0_s1.h:399`).
- A block is **data**. It is not executed until `do`, `values`, `reduce`,
  `either`, `judge`, a function body, or another form evaluates it.
- A bare block containing lexical BOUND references is not portable executable
  code; see the escape law (§6.3).
- A managed block produced by `reduce` has the same layout but lives in the
  managed heap. Managed blocks are data and are **not executable** as source
  (`values`, `reduce`, `do` and `judge` reject them by address with a
  fail-stop; §3.8).

### 1.5 `func` and `masm` forms

`func` (`r0_s1_runtime.c:783-858`):

```glon
func <spec-expression> <body-expression>
```

- Both spec and body are ordinary expressions and are evaluated at run time.
- The spec must evaluate to a block whose elements are the parameters; its
  length is the closure's arity.
- The body must evaluate to a block. A non-block body is a machine fail-stop
  (the law says `func` "fails loudly").
- Inside the canonical enclosing block, a **literal** body written directly
  after the spec gets a fresh lexical site and scope from the block parser. A
  **computed** body (`func :spec :body`, `func [] block-at rows i`) keeps the
  lexical origin it was parsed with. Canonical hosts always wrap source in one
  outer block, so this is the normal case. See §3.5.
- `func` is a keyword; a value-position `func` is never looked up as an
  ordinary word.

`masm`/`raw` (`parse_masm`, `r0_s1_runtime.c:686-760`):

```glon
masm [ ... ]          ;; canonical
masm 2 [ ... ]        ;; optional arity, default 0
raw  [ ... ]          ;; compatibility alias, same assembler path
```

- The block is a private S1 macro-assembly sublanguage, not ordinary Glon.
  Its mnemonics, labels and operands are not interned and are not lexically
  resolved.
- It produces a first-class RAW callable with layout `[entry, arity]`; it is
  invoked like a native/closure and may return any result set.
- Limits: 511 tokens, 64 labels, 128 label references, code-space bound
  `R0S1_CODE_LIMIT`; exceeding any limit is a parse failure
  (`too_large`), with no partial emission.
- RAW is a trusted, unsafe trapdoor. Ordinary programs must not need it.

### 1.6 Number and string limits

| Literal | Limit | On excess |
|---|---|---|
| integer | native: about ±2^59 (payload = cell/16); wasm32: about ±2^27 | C signed overflow / truncation; **UNRESOLVED** |
| string literal | 512 bytes | parse failure `too_large` |
| block | 512 forms (a string costs 2) | parse failure `too_large` |
| token | 63 bytes, silently truncated | no error; symbol collision possible |

---

## 2. Value model

### 2.1 Representation

**FROZEN.** Every Glon value is one cell:

```text
value = payload * 16 + tag
```

The low 4 bits are the tag. The implementation is `r0_s1.h:17-28`.

### 2.2 Tags

| Tag | Kind | Payload | Equality |
|---|---|---|---|
| 0 | INT | signed integer payload | numeric identity |
| 1 | NONE | 0 | only NONE |
| 2 | WORD | symbol id | symbol id (word family) |
| 3 | SET-WORD | symbol id | symbol id (word family) |
| 4 | GET-WORD | symbol id | symbol id (word family) |
| 5 | LIT-WORD | symbol id | symbol id (word family) |
| 6 | BLOCK | 16-aligned block address | cell identity |
| 7 | CONTEXT | context address | cell identity |
| 8 | CLOSURE | 16-aligned closure address | cell identity |
| 9 | NATIVE | native id | cell identity |
| 10 | RAW | RAW callable address | cell identity |
| 11 | USER | user-object address | cell identity |
| 12 | STRING | 16-aligned string payload | cell identity |
| 13 | BOUND | `depth * 16 + slot` | cell identity (internal) |
| 14 | SIN | SIN! payload address | cell identity |

There are no floats, characters, true/false values, vectors, maps, ports or
dictionaries. Use integers, words, blocks, strings, closures and NONE.

### 2.3 Word-family values

**FROZEN.** WORD, SET-WORD, GET-WORD and LIT-WORD compare by symbol id, so
`= 'x 'x` is true and a quoted word can dispatch against a plain word. Typical
programs only observe WORD values: evaluating `'x` yields WORD `x`, and
`x:` / `:x` are source forms rather than ordinary data.

### 2.4 Blocks

**FROZEN.**

- Loader blocks are permanent (loader heap) and are the source representation.
- Managed blocks come from `reduce`; they hold evaluated values.
- A block's `site_id` is static lexical metadata. It is not an activation
  identity and cannot make a bound block portable.
- A block is not automatically executed. A word bound to a block returns the
  block; it does not call it.

### 2.5 Closures

**FROZEN.** A closure is executable data with layout:

```text
[spec, body, captured-context, func-site-id, depth-bias]
```

(`CLOSURE_SPEC`, `CLOSURE_BODY`, `CLOSURE_CTX`, `CLOSURE_SITE`,
`CLOSURE_BIAS`, `r0_s1.h:389-394`).

A closure captures a real context identity. A captured stack-local context is
promoted to the managed heap when it escapes. The closure is the portable
executable form; it remains valid after its creator returns.

### 2.6 Strings

**FROZEN.** A STRING! is an immutable sequence of bytes:

```text
[length, byte0, byte1, ...]
```

- One cell per byte, values 0–255.
- No NUL termination, no Unicode semantics, no escape processing in literals.
- Operations return new strings; they never mutate an input.
- String content equality is `s/=`/`str-eq`; `=` is identity.
- String literals are built by `mk-string`; that word is library/prelude
  vocabulary, not a core native.

### 2.7 SIN! values

**FROZEN.** A SIN! is a first-class error **value** with three fields:

```text
[type, id, arg]
```

- `create-sin` constructs it; possession, storage, passing and comparison do
  not raise.
- `raise` propagates it; `judge` catches it. A SIN! equals only itself.
- A SIN! is inert data until raised.

### 2.8 Other runtime values

| Kind | Meaning | Ordinary visibility |
|---|---|---|
| NATIVE | Built-in native function | Bound to core words (§4) |
| RAW | First-class S1 callable | Only through `masm`/`raw` libraries |
| CONTEXT | Runtime environment | Internal; `context!` only in experimental M3 sessions |
| USER | User/object payload | Used by runtime internals and experimental M3 vocabulary |

---

## 3. Evaluation

### 3.1 Prefix evaluation and arity

**FROZEN.**

- Glon is prefix and fixed-arity. There is no operator precedence and there
  are no parentheses. The callee consumes the next complete expressions it
  needs.
- `+ 1 * 2 3` groups as `+ 1 (* 2 3)` and yields 7.
- `double double 5` groups as `double (double 5)`.
- Arguments are evaluated **eagerly, left-to-right**, before the call body.
- A call with too few following expressions is unchecked; it may read past the
  block or consume unrelated forms. A call with extra arguments leaves them
  for the surrounding expression stream. **UNRESOLVED**; do not rely on it.

### 3.2 Program and result sets

**FROZEN.**

- Evaluating a block evaluates each form in order. All result sets except the
  last are discarded; the last result set is returned.
- A result set is zero or more values. Most operations yield exactly one; a
  function can return several with `values` or `return values [...]`.
- The top-level host reports the final result set.
- `values [1 2]` yields two results. As the last expression of a program the
  host may show both: `1 2`.
- `[1 2]` as the last expression yields one result: the block itself.
- An empty block yields NONE; a function body that reaches its end yields the
  last form's result set.

### 3.3 Subexpression dispatch

**FROZEN.** Each form is evaluated by `emit_subexpr`
(`r0_s1_runtime.c:3569-3745`):

| Value at cursor | Action |
|---|---|
| INT, NONE, BLOCK, CONTEXT, CLOSURE, NATIVE, RAW, USER, STRING, SIN | self-evaluating as a value; if the value is a callable (closure/native/RAW) it is invoked with its arity arguments |
| WORD | dynamic lookup in the context chain; then dispatch the resulting value |
| BOUND | direct lexical slot read; then dispatch the value |
| SET-WORD | evaluate one following expression, assign to the nearest existing binding or create a local, yield the value |
| GET-WORD | look up the binding, yield it **without invoking** |
| LIT-WORD | yield a plain WORD with the same symbol id |
| BLOCK | yield the block itself, do not execute it |
| `func` keyword | consume spec and body, build a closure |
| `return` keyword | consume one expression and perform non-local return |

A plain word whose value is a CLOSURE, NATIVE or RAW **auto-invokes**. A plain
word whose value is a BLOCK, STRING, INT or any other non-callable yields that
value. Use `:word` to retrieve a closure/native/RAW without calling it.

### 3.4 Word lookup and assignment

**FROZEN.**

- A WORD is looked up dynamically through the current context and its lexical
  parent chain (`emit_lookup`, `r0_s1_runtime.c:2067-2187`).
- An unbound WORD is a **machine fail-stop**, not a SIN!: the run halts and
  cannot be caught by `judge`.
- A SET-WORD updates the **nearest existing binding** of that symbol in the
  chain. If no binding exists, it creates a new binding in the current
  context (a local inside a function, a global at top level).
- A GET-WORD performs the same lookup but never invokes the value.
- Parameter names are bound in the new function's context; inner parameters
  shadow outer parameters. Function specs are not lexically resolved against
  an enclosing function.
- Assignment is by value: integers, word ids and pointers are copied. Blocks,
  strings and contexts are referenced, not copied.

Lexical consequences:

```glon
x: 10
f: func [] [ x: + x 1  x ]   ;; updates the global x (nearest existing)
f                            ;; => 11
x                            ;; => 11
```

```glon
f: func [x] [ x: + x 1  x ]  ;; x is a parameter: updates the parameter
f 10                         ;; => 11
```

A set-word cannot store an activation-dependent block into a foreign context;
the escape law raises `SIN! 'escape 'store` before the store (§6.3).

### 3.5 Closures, lexical capture and the Guard of Binding

**FROZEN.**

- A literal body (`func [x] [ ... ]`) is the new function's own scope. It
  captures the current context with depth bias 0.
- A computed body (`func [] :body`, `func [] block-at rows i`) keeps the
  lexical meaning it had where the body was written:
  - a top-level block (site 0) binds to the global context;
  - otherwise the nearest live activation of the block's site is the origin;
    the closure captures it with depth bias +1;
  - if the origin activation is dead, the closure is marked with a sentinel
    site and any BOUND reference in it fails loudly. It never silently binds
    to an unrelated activation.
- A closure made from the running activation's own block resolves against
  that activation. A closure made from a factory closure's block resolves
  against the origin the factory captured.
- A captured stack-local context is promoted to the managed heap on escape.
  Promotion redirects the activation onto the managed copy, so the activation
  and all its closures share one context and mutations are visible both ways.

**Known limitation (FROZEN in the laws):** under same-site recursion, two live
activations share `FRAME_SITE`, so a travelling bare bound block resolves
against the innermost matching activation. Use a closure when
activation-specific capture is required.

### 3.6 `return`

**FROZEN.**

```glon
return <expression>
```

- Evaluates one expression and returns its result set from the **nearest
  enclosing function activation**, not from a helper, `do` block or `judge`.
- It abandons intermediate activations without running their epilogues.
- `return values []` returns zero results; `return values [a b]` returns two.
- `return` outside any function is a machine fail-stop.
- `return` inside a `judge` body unwinds through the judge to the enclosing
  function.
- `return` inside a CASE action returns from that action, because each CASE
  action is itself a zero-argument closure.

### 3.7 `do`

**FROZEN for block input; UNRESOLVED for other input.**

```glon
do <block>
```

- Evaluates the block in the **current context** and current activation.
- Saves and restores the current cursor/end/block pointers; it does not create
  a context.
- Returns the block's final result set.
- A block passed into another function and `do`ne there resolves plain words
  dynamically in the callee's context. Caller blocks that must retain lexical
  meaning should be turned into closures.
- `do []` yields NONE.
- The implementation does **not** type-check the argument before treating it
  as a block. A non-block argument is unchecked: its untagged payload is used
  as a block address, so it may read unrelated memory, return a nonsensical
  value, or halt. **UNRESOLVED**; the contract requires a block.

### 3.8 `values`, `reduce`, `either`, `invoke`

#### `values`

**FROZEN.**

```glon
values <block>
```

- Evaluates every form in the block and returns **all** results, preserving
  order.
- Each source expression must reduce to exactly one value. A form that yields
  zero or several results makes `values` fail-stop (`emit_values`,
  `r0_s1_runtime.c:2841-2887`). Therefore `values [values [1 2]]` halts.
- `values []` yields zero results.
- A managed block is not executable; `values` of a managed block is a
  fail-stop.

#### `reduce`

**FROZEN.**

```glon
reduce <source-block>
```

- Evaluates each source expression left-to-right, reduces each to **one**
  value, and returns a newly allocated managed block of those values.
- The source must be a block and must not be a managed block (the runtime
  rejects managed source blocks).
- A multi-result argument expression is collapsed to its **first** result by
  the internal `reduce` step. For example `reduce [values [1 2] 3]` yields
  `[1 3]`. This collapse is IMPLEMENTED and is not stated in the Alpha laws.
- `reduce []` yields an empty managed block.

#### `either`

**FROZEN for block branches; UNRESOLVED for other input.**

```glon
either <condition> <then-block> <else-block>
```

- Evaluates condition once, then exactly one branch block.
- Falsey values are NONE and integer 0. Everything else is truthy.
- Returns the taken branch's result set.
- Both branches must be blocks. The implementation does not type-check them;
  a non-block branch is used as a block address and is unchecked.
  **UNRESOLVED**.

#### `invoke`

**FROZEN.**

```glon
invoke <closure-producing-expression>
```

- Evaluates exactly one expression to obtain a CLOSURE value, then invokes it.
  The closure consumes its own arity arguments from the same caller expression
  stream.
- Use this for closures obtained from `:word`, `block-at`, `select`, or a
  factory call.
- A non-closure result is a machine fail-stop, not a catchable SIN!.

### 3.9 Optional control vocabulary

`case` and `parse` are ordinary Glon libraries, not natives. See §5.

---

## 4. Core vocabulary

**FROZEN** as bound by `r0_s1_init` unless marked otherwise
(`r0_s1_runtime.c:3841-3876`). Arity is the number of following expressions
consumed.

### 4.0 Vocabulary provenance

No ordinary Glon word is itself one of the seven frozen S1 primitives. The S1
instruction set is reachable only through `masm`/`raw`. The provenance of the
words a program can use is:

| Word or name | Provenance | Notes |
|---|---|---|
| `LIT`, `DUP`, `DROP`, `@`, `!`, `0BRANCH`, `HOST` | **FROZEN S1 instruction set** | Not ordinary Glon vocabulary; used only inside `masm`/`raw` blocks (and as assembler mnemonics). |
| `+`, `-`, `*`, `/`, `=`, `<`, `>`, `<=`, `>=`, `print` | **R0 native words over frozen S1 `HOST`** | The evaluator emits calls to `HOST_ADD`/`HOST_MUL`/`HOST_EQ`/`HOST_PRINT` etc. (`s1.c:134-160`). |
| `values`, `either`, `do`, `invoke`, `reduce`, `create-sin`, `sin?`, `sin-type`, `sin-id`, `sin-arg`, `raise`, `judge` | **R0 runtime natives** | Emitted above S1; not S1 primitives. |
| `func`, `return` | **Evaluator keywords** | Reserved words in value position; not S1 operations. |
| `masm`, `raw` | **Reader form + S1 trapdoor** | `masm`/`raw` exposes S1 instructions and the symbolic ABI; it is not portable application vocabulary. |
| `mk-string`, `str-eq`, `get`, `lambda`, `does`, `block-len`, `block-at`, `select`, `select-at` | **Glon library/prelude** | `jupyter/prelude.glon`, `demo/shop/bootstrap.glon`, `demo/shop/common.glon`. |
| `case`, `parse` | **Glon library** | `demo/shop/case.glon`, `demo/shop/parse.glon`. |
| `s/+`, `s/=`, `s/length`, `s/print`, `s/at`, `s/slice`, `s/span`, `s/scan-set`, `s/from-int` | **Glon library** | `strings.glon`, `strings-ext.glon`. |
| `spawn`, `yield`, `run-tasks`, `reset-tasks` | **Glon library** | `demo/shop/demos/tuple-space.glon` and equivalent M1 libraries. |
| `host_print`, `host_set_text`, `host_set_html`, `host_canvas_script`, `host_call` | **Host imports** | WASM host boundary; not Glon vocabulary. |
| `glon_alloc`, `glon_init`, `glon_load`, `glon_call`, `glon_route`, `glon_event`, `glon_event_value`, `glon_event_bytes`, `glon_run`, `glon_result_ptr`, `glon_result_len` | **Host exports** | WASM host boundary; not Glon vocabulary. |
| `current-route`, `current-event`, `current-value`, `route`, `do-event` | **Host/application convention** | G1A dispatch convention; `route`/`do-event` are application-defined blocks. |

`GLON-HOST-ABI.md` §1 restates this split for host authors.


### 4.1 Native operations

| Name | Arity | Result | Definition | Failure |
|---|---|---|---|---|
| `+` | 2 | one INT | integer addition | non-integer operands UNRESOLVED; overflow UNRESOLVED |
| `-` | 2 | one INT | integer subtraction | same |
| `*` | 2 | one INT | integer multiplication | same |
| `/` | 2 | one INT | truncating integer division | divide by zero UNRESOLVED |
| `=` | 2 | one INT (0 or 1) | type-safe identity/equality (§4.2) | none |
| `<` | 2 | 0/1 | integer less-than | non-integer UNRESOLVED |
| `>` | 2 | 0/1 | integer greater-than | same |
| `<=` | 2 | 0/1 | integer less-or-equal | same |
| `>=` | 2 | 0/1 | integer greater-or-equal | same |
| `print` | 1 | zero results | writes `int_val(arg)` and a newline | non-integer prints a payload-derived number; not a contract (**UNRESOLVED**) |
| `values` | 1 | all values | §3.8 | each form must yield exactly one value |
| `either` | 3 | branch result | §3.8 | non-block branch unchecked |
| `do` | 1 | block result | §3.7 | non-block unchecked |
| `invoke` | 1 expression | closure result | §3.8 | non-closure is a machine fail-stop |
| `reduce` | 1 | managed block | §3.8 | non-block/managed source is a fail-stop |
| `create-sin` | 3 | one SIN! | builds `[type id arg]` | none |
| `sin?` | 1 | 0/1 | 1 iff tag 14 | none |
| `sin-type` | 1 | field | first SIN! field | non-SIN raises `type/sin-field` (catchable) |
| `sin-id` | 1 | field | second field | same |
| `sin-arg` | 1 | field | third field | same |
| `raise` | 1 | does not return | propagate SIN! | non-SIN raises `type/raise` |
| `judge` | 1 | block results or SIN! | runs block in place; catches raises | non-block raises `type/judge` |

The implementation of arithmetic uses `int_val` (`payload / 16`) on both
operands without a tag check (`emit_native`, `r0_s1_runtime.c:3089-3260`).
The contract is arithmetic on integers. Behaviour on non-integers is
implementation-defined and must not be relied on.

### 4.2 Equality (`=`)

**FROZEN.**

- WORD, SET-WORD, GET-WORD and LIT-WORD compare by symbol id.
- Every other tag compares by full tagged cell identity (raw cell equality).
- Therefore:
  - `= none 0` is `0`;
  - `= none none` is `1`;
  - `= [1] [1]` is `0` (distinct blocks);
  - `= "a" "a"` is `0` unless both words evaluate to the same string object;
  - string content equality is `s/=` or `str-eq`;
  - `= 1 1` is `1`;
  - `= 'x 'x` is `1` (word family).

### 4.3 Keywords

| Form | Meaning | Failure |
|---|---|---|
| `func` | closure constructor (§1.5, §3.5) | non-block body: machine fail-stop |
| `return` | definitional return (§3.6) | outside a function: machine fail-stop |
| `masm [...]`, `masm N [...]` | assemble RAW callable | parse failure on bad/large block |
| `raw [...]`, `raw N [...]` | alias of `masm` | same |

### 4.4 SIN! and propagation

**FROZEN.**

A SIN! is a value; propagation is separate control flow.

```glon
e: create-sin 'demo 'oops 7
sin? e          ;; => 1
sin-type e      ;; => demo
sin-id e        ;; => oops
sin-arg e       ;; => 7
judge [ raise e ] ;; => #[SIN! demo oops 7]
```

- `raise err` abandons the current operation and unwinds to the nearest
  `judge` **in the same task**.
- `judge block` runs the block in the current activation/context. If it
  completes, `judge` returns its result set; if anything raises, `judge`
  returns the SIN! value and execution continues after the `judge`.
- Nested judges: the nearest catches.
- With no judge, the run ends as an uncaught SIN!; the host can read its
  fields.
- Judges are per task; a main-world judge does not catch a task's raise.
- Runtime checks raise SIN!s:
  - non-SIN! passed to `sin-type`/`sin-id`/`sin-arg`: `type/sin-field`;
  - non-SIN! passed to `raise`: `type/raise`;
  - non-block passed to `judge`: `type/judge`;
  - escape-law violations: `escape/<boundary>` where boundary is `return`,
    `argument`, `store`, `capture` or `transport`, and arg is the offending
    block's site id.
- Machine-integrity failures are not SIN!s and cannot be judged: out of
  memory, return-stack exhaustion, data-stack exhaustion, `CONTEXT_FULL`,
  GC worklist full, unbound word, non-closure `invoke`, non-block `func` body.

---

## 5. Standard libraries

These are ordinary Glon source, loaded by hosts or applications. They are
**IMPLEMENTED**, not frozen primitives. A bare runtime has only §4.

### 5.1 Jupyter prelude / `demo/shop/bootstrap.glon`

The same core definitions are shipped as `jupyter/prelude.glon` and copied
into the shop bootstrap (`demo/shop/bootstrap.glon`). Minimal definitions:

| Word | Signature | Semantics | Failure |
|---|---|---|---|
| `mk-string` | `[byte-int ...] -> STRING!` | copy tagged byte integers into a new managed string | unchecked if the block is malformed |
| `str-eq` | `STRING! STRING! -> 0/1` | byte-content equality; `s/=` is the same word | non-string raises `type/s/=` with the offending value |
| `get` | `WORD -> value` | raw lookup without invocation | unbound word fail-stops in lookup |
| `lambda` | `spec body -> CLOSURE` | `func :spec :body` | same as `func` |
| `does` | `body -> CLOSURE` | `lambda [] body` | same as `func` |
| `block-len` | `BLOCK -> INT` | number of elements | unchecked |
| `block-at` | `BLOCK INT -> value` | 0-based element access | unchecked; bad index reads unrelated memory |
| `select` | `rows key -> row or NONE` | first row whose first element `=` key | unchecked block shape |
| `select-at` | helper | recursive search | unchecked |

`mk-string` is required by every string literal. A runtime with only the core
natives cannot evaluate `"abc"`: the reader expands it to `mk-string [...]`,
and the unbound `mk-string` word is a machine fail-stop.

### 5.2 `case` (`demo/shop/case.glon`)

**IMPLEMENTED.**

```glon
case [
    [condition-1] [action-1]
    [condition-2] [action-2]
    ...
]
```

- Conditions are blocks, not inline expressions.
- First truthy condition's action is run; later conditions/actions are not
  evaluated. `case` returns the action's result.
- No match returns NONE.
- `[1]` is a catch-all condition.
- Conditions/actions run as zero-argument closures bound to their lexical
  origin, so words and assignments resolve as if written in the enclosing
  function.
- `return` inside an action returns from that action (and CASE yields the
  value), not from the enclosing function.
- A condition without an action, a non-block clause, or a dead-origin clause
  fails loudly (machine fail-stop).
- Each clause allocates a closure; recursion through CASE-using functions is
  bounded by promoted contexts (about 25 levels with the current heap).

### 5.3 `parse` (`demo/shop/parse.glon`)

**IMPLEMENTED.**

```glon
parse <input-block> <rules-block>   ;; => 1 or 0
```

- Operators are bare words: `skip`, `alt`, `opt`, `some`, `any`.
- `alt` consumes two following subrule blocks: `alt [A] [B]`.
- `opt`, `some`, `any` consume one following subrule block.
- Any other rule value is a literal matched with `=` against the next input
  value.
- Full-match law: true only if the rules succeed **and consume the entire
  input**.
- Repetition has a zero-width progress guard: a repeat that succeeds without
  advancing ends rather than looping forever.
- Internal failure sentinel is `-1`; positions are non-negative.
- No actions, captures, string parsing, `into`, or Rebol `/ALL`.

### 5.4 String library (`demo/shop/strings.glon`)

**IMPLEMENTED.** All operations are immutable and return new strings. Wrong
argument types raise `SIN! 'type <operation-name> <offending-value>`; both
arguments are evaluated before the check, so the first non-string is reported.

| Word | Signature | Semantics | Limits/errors |
|---|---|---|---|
| `s/+` | `STRING! STRING! -> STRING!` | concatenate bytes | non-string: `type/s/+` |
| `s/=` | `STRING! STRING! -> 0/1` | byte equality; same word as `str-eq` | non-string: `type/s/=` |
| `s/length` | `STRING! -> INT` | number of bytes | non-string: `type/s/length` |
| `s/print` | `STRING! -> no value` | write bytes then newline | non-string: `type/s/print` |

### 5.5 Optional string extension (`demo/shop/strings-ext.glon`)

**IMPLEMENTED optional.** Loaded after `strings.glon` only by sessions that
need it. Indices are 0-based byte offsets; ranges are half-open. No negative
indices and no clamping for extraction.

| Word | Signature | Semantics | Failure |
|---|---|---|---|
| `s/at` | `STRING! INT -> INT or NONE` | byte at index, or NONE outside `[0, length)` | non-string/non-int: `type/s/at` |
| `s/slice` | `STRING! INT INT -> STRING!` | `n` bytes from `start`; requires `0 <= start`, `0 <= n`, `start+n <= length` | bad range: `range/s/slice` arg `[start n]` |
| `s/span` | `STRING! INT INT -> STRING!` | bytes in `[start, end)`; requires `0 <= start <= end <= length` | bad range: `range/s/span` arg `[start end]` |
| `s/scan-set` | `STRING! INT STRING! -> INT` | first index `>= i` whose byte is not in the set; `i` clamped to `[0, length]`; returns length if none | wrong type: `type/s/scan-set` |
| `s/from-int` | `INT -> STRING!` | decimal text | non-int: `type/s/from-int` |

UTF-8 is not a language concern: a multi-byte character is several byte
indices, and slicing may split it.

### 5.6 Tasks (`demo/shop/demos/tuple-space.glon`)

**IMPLEMENTED optional; experimental in purpose.**

| Word | Signature | Semantics |
|---|---|---|
| `spawn` | `body-block -> INT` | create a cooperative task; return a task handle (`mk_int(record address)`, an implementation-defined integer), or -1 if full |
| `yield` | none | save this task's machine state and resume the scheduler |
| `run-tasks` | none | deterministic round-robin until no runnable task remains |
| `reset-tasks` | none | mark all task slots empty and reset the cursor |

- At most 3 tasks (`M1_MAX_TASKS`), fixed task arenas.
- Tasks are cooperative: they run only until `yield` or completion, and are
  resumed round-robin. There is no pre-emption, multicore or distribution.
- `spawn` checks the still-tagged body for activation-dependence and raises
  `SIN! 'escape 'transport` before creating a task if it would escape.
- Judges are per task; errors do not cross task boundaries.

---

## 6. Errors and failure model

### 6.1 Two failure classes

**FROZEN.**

| Class | Can `judge` catch? | Examples |
|---|---|---|
| SIN! propagation | yes, in the same task | `raise`, escape-law violations, `sin-field`, `judge` type |
| Machine fail-stop | no | unbound word, out of memory, stack exhaustion, `CONTEXT_FULL`, GC worklist, non-closure `invoke`, non-block `func` body, `return` outside a function |

A run that ends in a machine fail-stop is not clean. The host reports a halt,
not a SIN!.

### 6.2 Parse and load failures

**FROZEN categories; IMPLEMENTED mapping.**

| Kind | Meaning | Host detail |
|---|---|---|
| syntax | malformed source, unterminated block/string, bare `]`, missing `func` operand, bad masm | `syntax` |
| `too_large` | block/string over 512, masm block/labels/code over limit | `too_large` |
| `symbol_table_full` | more than 512 distinct symbols | `symbol_table_full` |
| `loader_exhausted` | loader heap cannot hold this source | `loader_exhausted` |
| `site_table_full` | more than 640 function sites | `site_table_full` |

A failed parse is rolled back completely: loader heap, symbol table and site
counter return to their prior state, and earlier session definitions remain
usable (`r0_s1_parse`, `r0_s1_runtime.c:4030-4064`).

### 6.3 Escape-time binding law

**FROZEN.** An activation-dependent block may be consumed or closed over while
its origin activation is live, but must not escape it. A closure is the
portable executable form. Enforcement points:

| Boundary | Check | Raise |
|---|---|---|
| frame exit | a frame must not return a dependent block of its own site or an ancestor site | `escape/return` |
| argument bind | a dependent block of site S must not be bound as an argument of a closure whose site is S | `escape/argument` |
| context store | a set-word must not store a foreign-site dependent block | `escape/store` |
| context capture/promotion | a context must not be captured while it holds a foreign-site dependent block | `escape/capture` |
| task transport | a task body must not be activation-dependent | `escape/transport` |

The raise's `arg` is the offending block's decoded site id. The violating
operation is abandoned before it completes; a judge never receives the illegal
value.

### 6.4 Runtime accessor errors

`sin-type`, `sin-id`, `sin-arg` on a non-SIN raise `SIN! 'type 'sin-field` with
the offending value as arg. `raise` on a non-SIN raises `SIN! 'type 'raise` with
that value. `judge` on a non-block raises `SIN! 'type 'judge` with that value.

---

## 7. State, memory and limits

**FROZEN memory model; IMPLEMENTED capacities.** Capacities are implementation
limits, not language semantics. Raising one is a separate, approved scaling
experiment.

### 7.1 Contexts

| Context | Capacity | On adding a new binding when full |
|---|---|---|
| global | 256 bindings | `CONTEXT_FULL` fail-stop before any write |
| each function activation | 16 bindings | `CONTEXT_FULL` fail-stop before any write |

Existing bindings can still be updated when a context is full; only a new
binding is refused. The rejected write leaves existing state uncorrupted.

### 7.2 Memory map (64K reference configuration)

| Region | Cells | Notes |
|---|---|---|
| registers/scratch | 0–255 | memory-mapped `IP/SP/RP/HP` |
| emitted runtime code | 256–~12,861 | grows with natives and masm libraries |
| main data stack | grows down from 16,384 | guarded |
| main return stack | grows down from 24,576 | frames, stack-local contexts, continuations |
| runtime state | 24,576–32,768 | `RV_*`, M1 scheduler, GC state, output buffers |
| managed heap | 32,768–40,000 (7,232 cells) | non-moving mark/sweep |
| loader heap | 40,000–54,800 (14,800 cells) | permanent; never swept |
| task arenas | 54,800–65,000 | 3 tasks |
| task table | 65,000–65,536 | |

The order and ranges are implementation details, not a portable language
contract. The 65,536-cell map is deliberate.

### 7.3 Resource limits

| Resource | Limit | Behaviour |
|---|---|---|
| managed heap | 7,232 cells; one cell per string byte | out-of-memory fail-stop; fragmentation can fail a large allocation with total free space present |
| smallest managed object | 32 cells | — |
| nested Glon calls | ~84 plain, fewer with wider frames | return-stack exhaustion fail-stop |
| block/string literal | 512 values / 512 bytes | parse failure `too_large` |
| symbols | 512 per session | parse failure `symbol_table_full` |
| function sites | 640 per session | parse failure `site_table_full` |
| loader heap | ~14,800 cells | parse failure `loader_exhausted` |
| masm code | `R0S1_CODE_LIMIT` = 16,128 | parse failure `too_large` |
| masm block | 511 tokens, 64 labels, 128 refs | parse failure `too_large` |
| data stack | code end + guard, or task base + guard | `DS_EXHAUSTED`/`DS_UNDERFLOW` halt |
| tasks | 3 | `spawn` returns -1 (its success handle is an implementation-defined integer) |

### 7.4 Garbage collection

**FROZEN model.** The managed heap is a non-moving, stop-the-world exact
mark/sweep collector. The loader heap is permanent and never swept. Every
GC-capable allocation occurs at a safepoint where all scanned state is valid
tagged Glon values. There are no stable handles, pinning or compaction.

### 7.5 Stack-local contexts and promotion

A function context normally lives on the return stack. When a closure captures
an escaping stack-local context, the context is promoted to the managed heap;
the owning activation is redirected to the managed copy. A stack-local context
that has never escaped is reclaimed with the activation.

---

## 8. Host boundary

Portable language semantics end at the host operations described in
**`GLON-HOST-ABI.md`**. In summary:

- The language itself supplies arithmetic, comparison, allocation, output and
  diagnostics through frozen S1 `HOST` operations. It does not supply file,
  network, browser DOM or GUI semantics.
- A host supplies string literals only in the sense that the prelude supplies
  `mk-string`; the reader's string expansion is language syntax but the word
  is library code.
- Browser route/event dispatch (`current-route`, `current-event`,
  `current-value`, `route`, `do-event`), WASM exports, the native session
  facade and the desktop capability boundary are host interfaces, not
  portable Glon semantics.
- The WebAssembly ABI is current/provisional, not frozen.

---

## 9. Explicit non-features (deferred)

**FROZEN as Alpha scope.** The language deliberately does **not** have:

- infix operators or operator precedence;
- parentheses or comma-separated argument lists;
- `switch`, `each`/`foreach`, `while`, `repeat`, `loop`;
- Rebol-style inline-condition `case` or `case/all`;
- full Rebol PARSE, string PARSE, PARSE actions, captures or `into`;
- a general `type?` in ordinary sessions;
- path evaluation, namespaces or qualified lookup;
- portable bare bound blocks;
- distributed closures or multicore tasks;
- floats, chars, maps, vectors, ports, `true`/`false` literals;
- mutable strings;
- comments as parser syntax (hosts strip `;;`).

Post-Alpha language changes require evidence from real machine-written
applications or a demonstrated correctness defect.

---

## 10. Traceability matrix

### 10.1 Core operation matrix

General limits (heap, stacks, contexts, block/string size, symbol/site counts)
are in §7.3 and are not repeated per row. "Input requirement" means the
expressions the operation consumes, not a runtime type check.

| Operation | Definition | Input requirement | Output/side effect | Failure | Tests |
|---|---|---|---|---|---|
| `+ - * /` | `emit_native`, `s1.c:134-160` | 2 expressions | one INT | non-integer/overflow/div-zero UNRESOLVED | `r0_s1_tests.c`, model tests |
| `< > <= >=` | same | 2 expressions | 0/1 | non-integer UNRESOLVED | `r0_s1_tests.c`, traffic/model tests |
| `=` | `r0_s1_runtime.c:3180-3235` | 2 expressions | 0/1 | none | `r0_s1_equality_tests.c` |
| `print` | `r0_s1_runtime.c:3098-3114` | 1 expression | writes number + newline; no result | non-integer UNRESOLVED | `r0_s1_tests.c` |
| `values` | `r0_s1_runtime.c:2841-2887` | block | all results; each form exactly one | machine fail-stop otherwise | `r0_s1_tests.c` J/K, `r0_s1_reduce_tests.c` |
| `either` | `r0_s1_runtime.c:3115-3145` | condition + 2 blocks | branch result | non-block unchecked | `r0_s1_tests.c`, `r0_s1_primer_tests.c` |
| `do` | `r0_s1_runtime.c:2948-2980`, `3145-3153` | block | executes in current context | non-block unchecked | `r0_s1_tests.c`, `r0_s1_bound_tests.c` |
| `invoke` | `r0_s1_runtime.c:3153-3180` | closure-producing expression | closure result | non-closure machine fail-stop | `r0_s1_invoke_tests.c` |
| `reduce` | `r0_s1_runtime.c:2889-2947` | loader block | managed block | non-block/managed machine fail-stop | `r0_s1_reduce_tests.c` |
| `create-sin` | `r0_s1_runtime.c:1804-1819` | 3 expressions | SIN! value | none | `r0_s1_error_tests.c` |
| `sin?` | `r0_s1_runtime.c:2981-3088` | 1 expression | 0/1 | none | `r0_s1_error_tests.c` |
| `sin-type`, `sin-id`, `sin-arg` | same | 1 expression | field value | non-SIN `type/sin-field` | `r0_s1_error_tests.c` |
| `raise` | `r0_s1_runtime.c:1821-1892`, `2981-3088` | 1 expression | unwind to judge | non-SIN `type/raise` | `r0_s1_error_tests.c` |
| `judge` | same | block | block result or SIN! | non-block `type/judge` | `r0_s1_error_tests.c` |
| `func` | `r0_s1_runtime.c:3583-3658` | spec + body expressions | closure; captures context | non-block body machine fail-stop | `r0_s1_lambda_tests.c`, `r0_s1_case_tests.c` B10 |
| `return` | `r0_s1_runtime.c:3463-3530` | 1 expression | non-local return with result set | outside function machine fail-stop | `r0_s1_tests.c` phase 3A, `r0_s1_case_tests.c` C17 |
| `masm`/`raw` | `r0_s1_runtime.c:686-760`, `281-620` | optional arity + block | RAW callable | parse `too_large`/`syntax` | `r0_s1_masm_tests.c`, `r0_s1_masm_bound_tests.c` |

### 10.2 Library operation matrix

Library operations are ordinary Glon; their limits are the general limits in
§7.3 plus the per-operation range rules in §5.

| Operation | Definition | Input requirement | Output/side effect | Failure | Tests |
|---|---|---|---|---|---|
| `mk-string` | `jupyter/prelude.glon`, `demo/shop/bootstrap.glon` | block of byte ints | new STRING! | unchecked | `r0_s1_string_ops_tests.c`, primer |
| `str-eq`, `s/=` | `demo/shop/common.glon`, `strings.glon` | 2 STRING! | 0/1 | `type/s/=` | `r0_s1_string_ops_tests.c` |
| `get` | prelude/common | WORD | binding value, no invoke | unbound lookup fail-stop | `r0_s1_invoke_tests.c`, lambda tests |
| `lambda`, `does` | prelude/common | spec/body or body | CLOSURE | same as `func` | `r0_s1_lambda_tests.c` |
| `block-len`, `block-at` | prelude/common | block, index | length/value | unchecked | `r0_s1_reduce_tests.c`, `r0_s1_invoke_tests.c`, case/parse |
| `select`, `select-at` | prelude/common | rows, key | first matching row/NONE | unchecked shape | `r0_s1_invoke_tests.c` |
| `case` | `demo/shop/case.glon` | one block of clause pairs | action result/NONE | malformed clause machine fail-stop | `r0_s1_case_tests.c` |
| `parse` | `demo/shop/parse.glon` | input and rules blocks | 1/0 | no raise on ordinary mismatch | `r0_s1_parse_tests.c` |
| `s/+ s/length s/print` | `demo/shop/strings.glon` | STRING! arguments | new string / INT / output | `type/<op>` | `r0_s1_string_ops_tests.c` |
| `s/at s/slice s/span s/scan-set s/from-int` | `demo/shop/strings-ext.glon` | types and ranges in §5.5 | byte/new string/INT/string | `type/<op>` or `range/<op>` | `r0_s1_string_ext_tests.c` |
| `spawn yield run-tasks reset-tasks` | `demo/shop/demos/tuple-space.glon` | task body | cooperative task scheduling | `escape/transport`; -1 when full | `r0_s1_m1_tests.c`, `r0_s1_error_tests.c`, primer |

### 10.3 Area matrix

This matrix maps the specification to implementation and conformance tests.
Line numbers are to the current worktree.

| Spec area | Implementation | Tests | Status |
|---|---|---|---|
| Value tags and cell encoding | `r0_s1.h:17-50` | `r0_s1_tests.c`, `r0_s1_equality_tests.c` | FROZEN |
| Reader: integers, words, prefixes | `r0_s1_runtime.c:177-216` | `r0_s1_tests.c`, `r0_s1_parse_hardening_tests.c` | IMPLEMENTED (forms FROZEN) |
| Reader: blocks, site ids, func scopes | `r0_s1_runtime.c:764-886` | `r0_s1_tests.c`, `r0_s1_bound_tests.c`, `r0_s1_case_tests.c` | FROZEN/IMPLEMENTED |
| Reader: strings → `mk-string` | `r0_s1_runtime.c:643-661`, `764-782` | `r0_s1_string_ops_tests.c`, primer tests | IMPLEMENTED |
| Reader: masm/raw | `r0_s1_runtime.c:686-760`, `281-620` | `r0_s1_masm_tests.c`, `r0_s1_masm_bound_tests.c` | IMPLEMENTED (trapdoor) |
| Top-level program / result sets | `r0_s1_runtime.c:3777-3810`, `3747-3775` | `r0_s1_tests.c` | FROZEN |
| Subexpression dispatch | `r0_s1_runtime.c:3569-3745` | `r0_s1_tests.c`, `r0_s1_invoke_tests.c` | FROZEN |
| Word lookup | `r0_s1_runtime.c:2067-2187` | `r0_s1_tests.c`, `r0_s1_p5_tests.c` | FROZEN |
| Set-word nearest binding/local creation | `r0_s1_runtime.c:2578-2643` | `r0_s1_tests.c`, `r0_s1_case_tests.c` | FROZEN |
| Bound lexical access / Guard of Binding | `r0_s1_runtime.c:2228-2291`, `2188-2227` | `r0_s1_bound_tests.c`, `r0_s1_p4_tests.c`, `r0_s1_case_tests.c` | FROZEN |
| Closures / capture / promotion | `r0_s1_runtime.c:2729-2888`, `3262-3462` | `r0_s1_tests.c`, `r0_s1_lambda_tests.c`, `r0_s1_nested_closure_tests.c`, `r0_s1_case_tests.c` | FROZEN |
| `func` / non-block body | `r0_s1_runtime.c:3583-3658` | `r0_s1_case_tests.c` B10 | FROZEN |
| `return` | `r0_s1_runtime.c:3463-3530` | `r0_s1_tests.c` phase 3A, `r0_s1_case_tests.c` C17 | FROZEN |
| `either` | `r0_s1_runtime.c:3115-3145` | `r0_s1_tests.c`, `r0_s1_primer_tests.c` | FROZEN |
| `do` | `r0_s1_runtime.c:2948-2980`, `3145-3153` | `r0_s1_tests.c`, primer tests | FROZEN (block input) |
| `values` | `r0_s1_runtime.c:2841-2887` | `r0_s1_tests.c` J/K, `r0_s1_reduce_tests.c` | FROZEN |
| `reduce` | `r0_s1_runtime.c:2889-2947` | `r0_s1_reduce_tests.c` | FROZEN |
| `invoke` | `r0_s1_runtime.c:3153-3180` | `r0_s1_invoke_tests.c` | FROZEN |
| Arithmetic/comparison | `r0_s1_runtime.c:3089-3260`, `s1.c:134-160` | `r0_s1_tests.c`, traffic/model tests | FROZEN on integers |
| `print` | `r0_s1_runtime.c:3098-3114` | `r0_s1_tests.c` | IMPLEMENTED |
| Equality | `r0_s1_runtime.c:3180-3235` | `r0_s1_equality_tests.c` | FROZEN |
| SIN! value/accessors | `r0_s1_runtime.c:1804-1892`, `2981-3088` | `r0_s1_error_tests.c` | FROZEN |
| `raise` / `judge` | `r0_s1_runtime.c:1821-1892`, `3120-3250` | `r0_s1_error_tests.c` | FROZEN |
| Escape law | `r0_s1_runtime.c:2388-2577` | `r0_s1_escape_law_tests.c`, `r0_s1_bound_tests.c` | FROZEN |
| Context capacities | `r0_s1.h:105`, `r0_s1_runtime.c:2568-2577` | `r0_s1_context_capacity_tests.c` | IMPLEMENTED limit |
| GC / memory | `r0_s1_runtime.c:1034-1770`, `r0_s1.h:226-338` | `r0_s1_m2_tests.c`, `r0_s1_gc_safepoint_tests.c`, `r0_s1_gc_interior_tests.c` | FROZEN model / IMPLEMENTED limits |
| Parse/load rollback | `r0_s1_runtime.c:4030-4064` | `r0_s1_parse_hardening_tests.c` | IMPLEMENTED |
| Session outcomes | `r0_s1_session.c` | `r0_s1_session_tests.c` | IMPLEMENTED |
| Prelude vocabulary | `jupyter/prelude.glon`, `demo/shop/bootstrap.glon` | `r0_s1_lambda_tests.c`, `r0_s1_invoke_tests.c`, `r0_s1_reduce_tests.c`, primer tests | IMPLEMENTED |
| `case` | `demo/shop/case.glon` | `r0_s1_case_tests.c` | IMPLEMENTED |
| `parse` | `demo/shop/parse.glon` | `r0_s1_parse_tests.c` | IMPLEMENTED |
| Strings | `demo/shop/strings.glon`, `demo/shop/common.glon` | `r0_s1_string_ops_tests.c` | IMPLEMENTED |
| String extension | `demo/shop/strings-ext.glon` | `r0_s1_string_ext_tests.c` | IMPLEMENTED optional |
| Tasks | `demo/shop/demos/tuple-space.glon`, `m1_layout.h` | `r0_s1_m1_tests.c`, `r0_s1_error_tests.c`, primer tasks examples | IMPLEMENTED optional |
| WASM ABI | `standalone/glon.c`, `GLON-WASM-ABI.md` | WASM node tests listed in that document | IMPLEMENTED/provisional |
| Native host facade | `r0_s1_session.c`, `r0_s1_show.c`, `glon.c` | `r0_s1_session_tests.c`, `r0_s1_primer_tests.c` | IMPLEMENTED |
| Frozen S1 | `s1.c`, `s1.h`, `tests.c`, `adversarial.c`, `claims.c` | `./check-frozen-s1.sh` | FROZEN |

---

## 11. Completeness audit

### 11.1 Can an independent agent write valid Glon from this document?

**Mostly yes, for the documented core and shipped libraries.** The core syntax,
evaluation order, binding, closures, result sets, SIN!, string operations and
limits are now specified. Before this consolidation, the following assumptions
forced a reader into the interpreter source:

1. **String literals need `mk-string` from a library.** The reader expands
   `"..."` into `mk-string [...]`; a bare core runtime halts. The Alpha laws
   describe strings but do not make this dependency explicit.
2. **Multi-result arguments are collapsed to their first value.** Native and
   closure argument evaluation reduces each argument to one value; `reduce`
   likewise keeps the first result. This is observable (`+ values [1 2] 3` is
   4) and is not in the laws.
3. **`values` requires exactly one result per source form.** A nested
   `values` inside `values` fail-stops. The primer does not state this.
4. **Non-block `do`/`either`, non-block `func` bodies, and missing/extra
   arguments are unchecked or fail-stop.** Only `func` non-block bodies are
   described by the laws; the rest are implementation-dependent.
5. **Comments are host pre-processing, not parser syntax, and are not
   string-aware.** The laws/primer show `;;` without saying the reader cannot
   parse it and that `;;` inside strings is stripped.
6. **Tokens are silently truncated at 63 bytes**, and the source envelope
   ignores trailing forms after a leading `[`.
7. **`none` is a literal token, not a binding.** `none:` can exist, but plain
   `none` stays NONE.
8. **Non-integer arithmetic and `print` are not type-checked.** The contract is
   integer arithmetic; the implementation computes on `payload/16`.
9. **`reduce` rejects managed source blocks** (a data/execution separation
   rule that is described only in runtime comments).
10. **`masm`/`raw` is a private, unsafe sublanguage** whose mnemonics are not
    ordinary vocabulary.

### 11.2 Prioritised documentation gaps

| Priority | Gap | Where it bites |
|---|---|---|
| P0 | Result-set collapse to first value in argument/reduce position | incorrect arithmetic and data construction |
| P0 | `values` exactly-one-result rule | silent `** halted` on nested/multi-result forms |
| P0 | String literal → `mk-string` library dependency | string programs fail on a bare runtime |
| P0 | Unchecked non-block `do`/`either`, missing args | memory-unsafe or nonsensical results |
| P1 | Non-integer arithmetic/print and division-by-zero contract | portability, native vs WASM divergence |
| P1 | Host comment stripping, including inside strings | source corruption and parse errors |
| P1 | Token truncation at 63 bytes and symbol capacity | surprising collisions |
| P1 | Source envelope ignores trailing forms | silently skipped program text |
| P1 | `reduce` managed-source rejection | confusing halt after `reduce` |
| P1 | `none` versus `none:`/`'none` | binding and equality mistakes |
| P1 | `return` outside function and no-argument `return` | fail-stop vs value confusion |
| P2 | `func` spec non-block/duplicate parameters | undefined arity and slot layout |
| P2 | `masm` grammar, labels and register ABI | trapdoor use requires source reading |
| P2 | `print` exact formatting for all values | host-dependent output |
| P2 | `judge` result-set behaviour for multi-result blocks | result loss in catch paths |
| P2 | Task scheduling details beyond 3 slots | concurrency expectations |

### 11.3 Missing conformance tests (prioritised)

1. Argument collapse: `+ values [1 2] 3`, `print values [1 2]`,
   `either values [0 1] [1] [2]`, `reduce [values [1 2] 3]`.
2. `values` rejection of zero/multi-result forms and nested `values`.
3. `do`/`either` with non-block arguments: document a controlled failure or
   an explicit undefined contract.
4. Missing and extra function arguments (currently reads beyond the block or
   leaves forms behind).
5. `func` with a non-block spec, duplicate parameter names and more than 16
   parameters.
6. `return` outside a function; `return` with no following expression.
7. String literal with `;;` inside; unterminated string; backslash and quote
   handling.
8. Token of 64+ bytes and two long tokens sharing a 63-byte prefix.
9. `none: 5  none` and `'none` equality/molding.
10. `reduce` of a managed block (`reduce reduce [1]`).
11. Non-integer operands for every arithmetic/comparison native.
12. Division/modulo by zero (native crash versus WASM trap).
13. Source beginning with a block followed by more forms.
14. `print` of NONE, word, block, string and closure.
15. Host comment stripping of `;;` inside a string through each host path.
16. Form adjacency and reader boundaries: `a[1]`, `1"x"`, `a"b"`,
    `func[x][y]`, `func "x" [1]` inside a block, `masm[ ]`, `masm 1[]`,
    `masm1[]`, `masm -1 []`.
17. Top-level unwrapped `func` versus the same form inside a block.
18. Token truncation at 63 bytes and two long tokens sharing a prefix.

### 11.4 Contradictions and evidence gaps

- **`print` and arithmetic types.** The primer says `print` prints integers
  only and that arithmetic operates on integers. The implementation performs
  no type check. This specification treats non-integer behaviour as
  UNRESOLVED rather than choosing one description.
- **`do`/`either` non-block failure.** The laws describe them as taking
  blocks; the implementation has no type guard. No test pins the failure.
- **`values` multi-result acceptance.** The laws say every expression is
  reduced to a single value; the implementation fail-stops when a form does
  not have exactly one result. The exact rule is only visible in
  `emit_values`.
- **Traffic test 8.** `GLON-ARCHITECTURE-DECISION-2026-11.md` records an
  unresolved traffic-test discrepancy (test 8 fail-stop) in this environment,
  separate from the language contract. It is a verification gap, not evidence
  for a language change.

---

## 12. Test report for this audit

Commands actually run in this worktree (2026-11-17):

| Command | Result |
|---|---|
| `./check-frozen-s1.sh` | PASS: frozen at `f90496c…`; frozen files unchanged |
| `./gc-worklist-overflow-test` | PASS: all checks |
| `python3 demo/shop/build-primer.py --check` | PASS: 30 examples up to date |
| Core groups (`r0_s1_tests`, lambda, reduce, parse, equality, bound, case, error, escape law, strings, strings-ext, invoke, session, context capacity, parse hardening, masm, nested closure) | 464 `ok`, 0 failures (temporary test driver, same objects as `make test`) |
| Remaining non-traffic groups (S1, adversarial, claims, R0, debugger, M1, M2, M3/M3B/M3C/M3D, P4, P5, G1A–G1E, GC safepoint, primer, GC worklist, GC interior, masm bound) | 420 `ok`, 0 failures |
| Vehicle-model groups (Newell, OVM/FVDM, NaSch) | 38 `ok`, 0 failures |
| Full `./s1` | Did not complete: it reached the traffic group, printed a `[dump]` at test 8 and remained CPU-bound. This matches the already-recorded unresolved traffic-test-8 discrepancy; it is not reproduced by the 922 passing checks above. |

The test driver was a temporary file under `/tmp`, linked against the
repository's already-built object files. No repository source, test or build
file was changed.

### 12.1 Final verification pass

The follow-up documentation pass verified every EBNF production against
`parse_int`, `parse_word`, `parse_string_literal`, `parse_masm`, `parse_block`,
`parse_form` and `parse_program`; corrected the item/form distinction, zero
width separators, func body optionality, unsigned masm arity and token
truncation; and mapped every Mermaid diagram to its EBNF production. Details
and remaining ambiguity are in `GLON-SPEC-VERIFICATION.md`.

No original contract was replaced. The detailed WASM ABI remains
`GLON-WASM-ABI.md`; no repository artifact naming Rebolek was found.

---

## 13. Source and test index

Authoritative documents:

- `GLON-ALPHA-LAWS.md` — normative language contract.
- `GLON-ARCHITECTURE-MEMORY.md` — decisions, memory map, limits.
- `GLON-WASM-ABI.md` — original detailed WASM ABI.
- `GLON-SPEC-VERIFICATION.md` — final EBNF/diagram/ABI provenance audit.
- `GLON-IMPLEMENTER-GUIDE.md` — minimum reading path for independent implementers.
- `GLON-HOST-CAPABILITIES.md`, `GLON-APP-MODEL.md` — host/application model.
- `GLON-PRIMER.md` — instructional primer; subordinate to the laws.

Implementation:

- `r0_s1_runtime.c` — reader, emitter, evaluator, GC, SIN!, escape law.
- `r0_s1.h` — tags, layouts, native ids, capacities, memory constants.
- `r0_s1_session.c` — persistent session outcome classification.
- `r0_s1_show.c` — text outcome/molding.
- `s1.c`, `s1.h` — frozen substrate.
- `m1_layout.h` — task layout.

Libraries:

- `jupyter/prelude.glon` — minimal prelude.
- `demo/shop/bootstrap.glon` — shop bootstrap copy.
- `demo/shop/common.glon` — `str-eq`, `get`, `lambda`, `does`, block access,
  `select`, view vocabulary.
- `demo/shop/strings.glon`, `demo/shop/strings-ext.glon`.
- `demo/shop/case.glon`, `demo/shop/parse.glon`.
- `demo/shop/demos/tuple-space.glon` — tasks.

Tests:

- Core: `r0_s1_tests.c`, `r0_s1_lambda_tests.c`, `r0_s1_reduce_tests.c`,
  `r0_s1_parse_tests.c`, `r0_s1_equality_tests.c`, `r0_s1_bound_tests.c`,
  `r0_s1_case_tests.c`, `r0_s1_error_tests.c`, `r0_s1_escape_law_tests.c`,
  `r0_s1_invoke_tests.c`, `r0_s1_nested_closure_tests.c`,
  `r0_s1_masm_tests.c`.
- Strings: `r0_s1_string_ops_tests.c`, `r0_s1_string_ext_tests.c`.
- Sessions/resources: `r0_s1_session_tests.c`,
  `r0_s1_context_capacity_tests.c`, `r0_s1_parse_hardening_tests.c`.
- Memory/tasks: `r0_s1_m1_tests.c`, `r0_s1_m2_tests.c`,
  `r0_s1_gc_safepoint_tests.c`, `r0_s1_gc_interior_tests.c`,
  `r0_s1_gc_worklist_tests.c`.
- Models: `r0_s1_newell_tests.c`, `r0_s1_ovm_tests.c`,
  `r0_s1_nasch_tests.c`, `r0_s1_traffic_tests.c`.
