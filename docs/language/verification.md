# Glon Specification Verification

**Date:** 2026-11-17
**Scope:** documentation-only verification of `GLON-LANGUAGE-SPEC.md`,
`GLON-SYNTAX-DIAGRAMS.md`, `GLON-EVALUATION-MODEL.md`, and `GLON-HOST-ABI.md`
against the implemented reader/evaluator and the original ABI document. No
implementation, frozen S1, prelude, application or test file was changed.

---

## 1. Original ABI/specification and Rebolek

### 1.1 Search result

A search of this worktree, the main worktree, all reachable refs, all
unreachable Git objects, and the local project notes/downloads found **no Glon
artifact that names Rebolek**. There is no file, commit message, tag, or Git
note in this repository that records such an attribution.

The only local mention of Rebolek found outside the source tree is a project
memory note, not an ABI or specification:

- `/mnt/c/Users/anon_/Downloads/glon-memory-handoff-2026-10-10.md`
  (`## Recoil / Rebolek side note`, lines 245–251).
  It records that Graham considered asking Rebolek about his work and says
  public docs do not clearly expose a Glon VM implementation. It contains no
  Glon ABI.

Therefore this verification does **not** claim that any repository file was
"prepared for Rebolek". If an external copy exists outside these paths, it was
not available to this audit.

### 1.2 The original ABI documents that do exist

| Document | Exact path | First commit | Date | Role |
|---|---|---|---|---|
| Glon WASM ABI | `GLON-WASM-ABI.md` | `e2edb0086664a7ad44c1487299933cda365d80a2` ("Document the Glon WASM ABI") | 2026-10-06 | The detailed, current/provisional WASM binary boundary. |
| Transient allocation lifetime | `GLON-WASM-ABI.md` §3.2, §10 | `3f5780037a09069c24413b3c41015819049be22e` ("Fix transient Glon WASM ABI allocation lifetime"), tag `glon-wasm-abi-d11.6` | 2026-10-05 | D11.6 one-execution-entry pointer lifetime. |
| Host capability architecture | `GLON-HOST-CAPABILITIES.md` | `92e7e9a3f627a4a94c09576309187f1a5e65669e` ("Document Glon host capability architecture") | 2026-10-04 | Host capability/permission model, not the binary ABI. |
| Language laws | `GLON-ALPHA-LAWS.md` | `b178567f9b86af248752b816a61545b048a4eb5b` ("Document Glon Alpha language laws") | 2026-09-22 | Normative language contract, not the host ABI. |

`GLON-HOST-ABI.md` is explicitly a derivative: for the WASM boundary,
`GLON-WASM-ABI.md` and `standalone/glon.c` remain authoritative. The original
contract was not replaced.

---

## 2. Comparison: `GLON-WASM-ABI.md` vs `GLON-HOST-ABI.md`

### 2.1 Agreements

- Both mark the WASM ABI **current/provisional**, with no version constant or
  compatibility guarantee; both point to `standalone/glon.c` as source of truth.
- Same module variants: W2 standalone, G1A shop, GLON_LIVE.
- Same common exports: `glon_alloc`, `glon_init`, `glon_load`, `glon_call`,
  `glon_run`, `glon_result_ptr`, `glon_result_len`, `memory`.
- Same host imports: `host_print`, `host_set_text`, `host_set_html`,
  `host_canvas_script`, and GLON_LIVE `host_call`.
- Same G1A exports: `glon_route`, `glon_event`, `glon_event_value`; GLON_LIVE
  adds `glon_event_bytes`.
- Same execution-entry lifetime rule: `glon_alloc` pointers are valid only
  until the next `glon_load`/`glon_call`/`glon_route`/`glon_event`/
  `glon_event_value`/`glon_event_bytes`/`glon_run`.
- Same arena model: separate 64 KiB transient `abi_heap`, 8-byte alignment,
  `len + 1` reserved, reset at each execution entry.
- Same source/token/value limits: source < 16 KiB; `glon_call` name <= 60;
  route/event token <= 63; `glon_event_value` value <= 200; `glon_event_bytes`
  data <= 12000; rendered HTML 16 KiB; canvas 2560 bytes.
- Same result lifetime: `glon_run` result buffer is static, replaced by the
  next `glon_run`; route/event output goes through imports, not the result
  buffer.
- Same singleton/re-entrancy rule: one global machine, serialised calls, no
  re-entry from host callbacks.
- Same tested-host list and same `./check-frozen-s1.sh` role.

### 2.2 Omissions in the derivative (now recorded or corrected)

| Original detail | Status in `GLON-HOST-ABI.md` |
|---|---|
| `glon_init` is idempotent and seeds M1 cells | Added in §4.2. |
| `glon_alloc` reserves `len + 1` and is 8-byte aligned | Present in §4.4. |
| `host_set_html` handle is hard-coded to 1 | Present in §4.3. |
| `host_canvas_script` is called after every dispatch, possibly empty | Added in §4.3. |
| `glon_run` result buffer is 4 KiB | Added in §4.4. |
| Committed `standalone/glon.wasm` is stale | Added in §4.1. |
| Route delimiter check is in G1A, not the WASM export | Corrected in §4.5. |
| `glon_event_value` truncates at embedded NUL | Present in §4.5. |
| Per-function return codes | Present in §4.2. |
| Appendix export sets and tested-host table | Summarised; full original remains authoritative. |

### 2.3 Contradictions

No substantive contradiction was found. Two earlier imprecisions were fixed:

1. `GLON-HOST-ABI.md` originally said the WASM host layer checks both token
   length and delimiters. The original is precise: length is checked in the
   host export; delimiters are checked in `r0_s1_g1a_route`/`_event` and mapped
   to `-2`. Corrected in §4.5.
2. `GLON-HOST-ABI.md` originally did not mention that the committed
   `standalone/glon.wasm` predates `glon_run`/`masm`. Added in §4.1 so an
   implementer does not test a stale binary.

The original document was not edited or replaced.

---

## 3. EBNF production verification

The reader is `r0_s1_runtime.c`: `parse_int`/`parse_word` at 177–216,
`parse_string_literal` at 643–661, `parse_masm` at 686–760, `parse_block` at
764–886, `parse_form` at 887–893, `parse_program` at 3963–4002.

| EBNF production | Implementation check | Result |
|---|---|---|
| `source = ws* ( block trailing-tail | top-item ( ws* top-item )* ws* )` | `parse_program`: skip ws; if `[` parse one block and return (trailing ignored); otherwise loop top-items to end. | **Matches.** Corrected the earlier grammar (it wrongly made `form` carry string/func/masm and required whitespace between forms). |
| `trailing-tail = any-byte*` | `parse_program` returns immediately after `parse_block`; no trailing check. | **Matches** (`[1] 2` parses as `[1]`). |
| `top-item = string | masm-form | form` | `parse_program` handles a leading `"` as string, exact `masm`/`raw` via `parse_masm`, otherwise `parse_form`. It does **not** special-case `func`. | **Matches**; top-level `func` is a WORD. |
| `block = "[" ( ws | block-item )* "]"` | `parse_block`: `[`, loop ws/string/func/masm/form, `]`; unclosed or empty token is syntax error. | **Matches.** |
| `block-item = string | func-form | masm-form | form` | `parse_block` string branch at 776–782; func branch at 783–858; masm branch at 859–870; else `parse_form`. | **Matches.** |
| `form = block | integer | word` | `parse_form`: `[` → block; digit or `-`+digit → integer; else word. No string/func/masm. | **Matches**; important for spec/body position. |
| `func-form = "func" form block?` | `parse_block` recognises exact token `func`, parses spec with `parse_form`, then if next nonws is `[` parses a literal body block under a fresh site/lexical scope; otherwise leaves the body in the stream. | **Matches**, with the note that this is block-only and body may be computed. |
| `masm-form = ( "masm" | "raw" ) ( ws+ digit+ )? ws* masm-block` | `parse_masm`: optional non-negative integer only when the first nonws char is a digit; then requires `[`; tokenizes privately. Exact keyword detection occurs in both block/program loops. | **Matches.** Earlier grammar allowed a signed integer; actual arity is unsigned. |
| `masm-block = "[" masm-tokens "]"` | Private masm tokenizer; not Glon forms; limits 511 tokens / 64 labels / 128 refs. | **Matches as an opaque terminal.** |
| `string = '"' non-quote-byte* '"'` | `parse_string_literal`: reads to next `"`, no escapes, max 512; item-start only; expands to `mk-string [bytes]`. | **Matches.** Earlier grammar omitted item-start restriction and expansion. |
| `integer = [ "-" ] digit+` | `parse_int`; called only when first char is digit or `-` followed by digit. | **Matches.** |
| `word = token` | `parse_word`: maximal run until ws/`[`/`]`; no other delimiters. | **Matches.** |
| `token = token-byte+` | `parse_word` returns token; reader truncates to 63 bytes before interning. | **Matches** with the truncation note; earlier grammar incorrectly said 1..63 (actual accepts longer and truncates). |
| `token-byte = ? any byte except whitespace, "[" or "]" ?` | `parse_word` stops at whitespace, `[` and `]`; every other byte is part of the token. | **Matches.** |
| `non-quote-byte = ? any byte except double quote ?` | `parse_string_literal` reads until the next `"`. | **Matches.** |
| `any-byte = ? any byte 0..255 ?` | `parse_program` ignores all bytes after a leading block. | **Matches.** |
| `word-value` first-match rules | `parse_word`: exact `none`; final `:`; leading `:`; leading `'`; else word. | **Matches.** The EBNF is ambiguous by itself because `token` contains `:`/`'`; the prose order is normative. |
| `digit = "0" | ... | "9"` | `parse_int` consumes ASCII `0`–`9` only. | **Matches.** |
| `ws` | `skip_ws` uses C `isspace`: space, tab, LF, CR, VT, FF. | **Matches.** |

### 3.1 Corrections made during this pass

The EBNF in both the specification and syntax-diagram documents was corrected
to the verified version above:

- separated `top-item` (no `func`) from `block-item` (includes `func`);
- separated `item` recognition from `parse_form`'s `form`;
- allowed zero-width separators where delimiters make the boundary
  unambiguous;
- made `func`'s body block optional (computed body left in the stream);
- restricted masm arity to unsigned digits;
- made token length open-ended with a 63-byte truncation note;
- noted string item-start restriction, expansion and 512-byte limit;
- noted the leading-block trailing-tail behaviour;
- completed the terminal alphabets (`digit`, `any-byte`, `token-byte`,
  `non-quote-byte`) and quoted the whitespace terminals so the grammar is
  machine-readable.

These are documentation corrections, not language changes.

### 3.2 Observed reader boundary cases

Parsed with a temporary probe linking the current `r0_s1_runtime.c` (no
repository change):

| Source | Parsed result | Confirms |
|---|---|---|
| `a[1]` | `[W(a) [1]]` | zero-width separator before a delimiter |
| `1"x"` | `[1 mk-string [120]]` | integer followed by a string with no whitespace |
| `a"b"` | `[W(a"b")]` | quote inside a maximal word token |
| `[ func[x][y] ]` | `[W(func) [W(x)] [W(y)]]` | func special case inside a block |
| `[ func "x" [1] ]` | `[W(func) W("x") [1]]` | spec string is a word, not a literal |
| `func[x][y]` (unwrapped) | `[W(func) [x] [y]]` | top-level `func` is not special-cased |
| `[1] 2` (raw parse) | `[1]` | leading block ignores trailing forms |
| `masm1[]` | `[W(masm1) []]` | keyword must be the whole token |
| `masm[ ]` | parse error | empty masm block is invalid at validation |
| `masm 1[]` | parse error | empty masm block is invalid at validation |
| `masm -1 []` | parse error (syntax) | masm arity is unsigned |
| `-1` | `[-1]` | negative integer |
| `-x` | `[W(-x)]` | `-` not followed by a digit is a word |
| `]` (top level) | parse error (syntax) | empty token is malformed source |

`mk-string` in the table is the expansion the reader emits for a string
literal, not a source word.

---

## 4. Mermaid diagram verification

The diagrams in `GLON-SYNTAX-DIAGRAMS.md` are control-flow renderings of the
verified EBNF, not a second formal grammar. The new §13 map states the
correspondence:

| Diagram | EBNF | Verified? |
|---|---|---|
| §1 Source envelope | `source`, `trailing-tail` | Yes; shows leading block/trailing ignored and top-item loop. |
| §2 Item alternatives | `top-item`, `block-item` | Yes; now notes top-level omits `func`. |
| §2 Form alternatives | `form` | Yes; now excludes string/func/masm. |
| §3 Block | `block`, `block-item` | Yes; string costs two forms, 512 limit. |
| §4 Word forms | `word`, `token`, `word-value` | Yes; added 63-byte truncation before rules. |
| §5 Integer | `integer` | Yes. |
| §6 String | `string` | Yes; item-start, no escapes, 512, expansion. |
| §7 Func | `func-form` | Yes; now labelled block-only; computed body left. |
| §8 Masm | `masm-form` | Yes; exact keyword, unsigned arity. |
| §9 Grouping | none | Correctly labelled as evaluation, not syntax. |

Remaining limitation: Mermaid cannot express maximal-munch tokenization or
the 512-form counter. The prose and EBNF carry those constraints.

---

## 5. Vocabulary provenance verification

`GLON-LANGUAGE-SPEC.md` §4.0 now classifies every documented vocabulary group:

- **Frozen S1 instruction set:** `LIT DUP DROP @ ! 0BRANCH HOST` — reachable
  only through `masm`/`raw`, never ordinary Glon words.
- **R0 natives over S1 `HOST`:** arithmetic, comparison, `print`.
- **R0 evaluator mechanics:** `values`, `either`, `do`, `invoke`, `reduce`,
  SIN! words, `func`, `return`.
- **Reader trapdoor:** `masm`/`raw`.
- **Glon libraries:** prelude/common, `case`, `parse`, strings, tasks.
- **Host imports/exports and dispatch convention:** `host_*`, `glon_*`,
  `current-*`.

`GLON-HOST-ABI.md` §1 independently repeats the portable-vs-host split. No
vocabulary entry now stands unclassified.

One nuance to preserve: arithmetic and comparisons are ordinary Glon native
words even though their implementation calls frozen S1 `HOST` operations. They
are not S1 primitives and are not library code.

---

## 6. Independent-implementer reading guide

See `GLON-IMPLEMENTER-GUIDE.md`. Minimum set:

1. `GLON-ALPHA-LAWS.md` — normative semantics.
2. `GLON-LANGUAGE-SPEC.md` — complete descriptive surface and vocabulary.
3. `GLON-SYNTAX-DIAGRAMS.md` — verified lexical grammar.
4. `GLON-EVALUATION-MODEL.md` — execution, binding, result sets, errors.
5. `GLON-HOST-ABI.md` + `GLON-WASM-ABI.md` — host boundary.
6. `GLON-ARCHITECTURE-MEMORY.md` — decisions and limits.
7. The named conformance tests for the behaviour being implemented.

The C source is needed only for unresolved details such as `masm` mnemonics,
exact GC layout or host-specific pointer formats. It is not needed for ordinary
Glon programs.

---

## 7. Remaining ambiguity and missing conformance tests

### 7.1 Ambiguity / unresolved behaviour

1. **Non-integer arithmetic and `print`.** Contract says integers; runtime
   computes on `payload / 16` without a type check.
2. **Division/modulo by zero.** C undefined behaviour versus WASM trap; no
   contract.
3. **Non-block `do`/`either`.** Used as a block address; may read unrelated
   memory or return nonsense.
4. **Missing/extra call arguments.** Missing args read past the block; extra
   args remain for the following expression.
5. **`func` spec shape.** A non-block spec, duplicate parameters, or more than
   16 parameters are not validated before use.
6. **`return` with no expression / at end of block.** The reader consumes the
   next form, which may be past the end.
7. **`values` exactly-one-result rule.** Implementation fail-stops on zero or
   multiple results per form; laws/primer do not state it.
8. **Result-set collapse.** Arguments and `reduce` keep the first of several
   results; undocumented.
9. **String literal library dependency.** `"..."` expands to `mk-string [...]`;
   a bare core runtime halts.
10. **Host comment stripping.** `;;` is not parser syntax and is stripped even
    inside strings.
11. **Token truncation at 63 bytes** and silent symbol collision.
12. **Leading block ignores trailing source forms.**
13. **`reduce` rejects managed source blocks.**
14. **`none` literal versus `none:`/`'none`.**
15. **`masm` empty-block validation** fails after parsing, not as a lexical
    rule.

### 7.2 Missing conformance tests (prioritised)

1. Argument collapse: `+ values [1 2] 3`, `print values [1 2]`,
   `reduce [values [1 2] 3]`.
2. `values` zero/multi-result forms and nested `values`.
3. Non-block `do`/`either`, and missing/extra function arguments.
4. `func` non-block spec, duplicate parameters, 17+ parameters.
5. `return` outside a function and `return` at end of block.
6. String with `;;` inside, unterminated string, backslash/quote handling.
7. 64+ byte token and two tokens sharing a 63-byte prefix.
8. `none: 5 none` and `'none` equality/molding.
9. `reduce reduce [1]` (managed source).
10. Non-integer operands for each arithmetic/comparison word.
11. Division and modulo by zero on native and WASM.
12. Source starting with a block followed by more forms.
13. `print` of NONE, word, block, string and closure.
14. `func` at unwrapped top level versus inside a block (site/scope).
15. `func "x" [1]` inside a block (spec is a word, not a string).
16. `masm[ ]`, `masm 1[]`, `masm1[]`, and `masm -1 []` lexical cases.
17. Form adjacency: `a[1]`, `1"x"`, `a"b"`.
18. Host comment stripping through each host path (native session, WASM
    `glon_load`, `glon_run`, CLI).

The current suite already covers the main language laws, closures, case,
parse, strings, SIN!, escape law, memory/GC, tasks and primitive models; these
gaps are concentrated at lexical boundaries, result-set collapse and unchecked
non-block/argument cases.
