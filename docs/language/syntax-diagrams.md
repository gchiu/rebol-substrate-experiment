# Glon Syntax Diagrams

This file diagrams the grammar the current reader actually implements. It is
descriptive: `GLON-ALPHA-LAWS.md` is normative and the reader is the ground
truth for lexical detail. The productions and diagrams were verified against
the reader in `GLON-SPEC-VERIFICATION.md` §3–§4. See
`GLON-LANGUAGE-SPEC.md` §1 for prose.

## Notation

- `[` and `]` in a diagram are literal Glon brackets; `{ ... }` means
  repetition; `?` means optional.
- The reader has no comment syntax. A host strips `;;` comments before the
  reader sees them.
- Whitespace separates forms. There is no parenthesis, comma, escape or
  comment grammar.
- `form` means a complete expression as the evaluator sees it. Argument
  grouping is decided at run time by callee arity, not by punctuation.

---

## 1. Source envelope

The canonical host form is one outer block. The bare parser also accepts an
unwrapped top-level sequence.

```mermaid
flowchart TD
    A["source"] --> B{"first non-space byte"}
    B -->|"open bracket"| C["block"]
    B -->|"anything else"| D["top-item (ws* top-item)*"]
    C --> E["parsed program block"]
    D --> F["implicit top-level block"]
    C --> G["trailing forms after this block are ignored"]
```

The trailing-forms behaviour is an implementation quirk of `parse_program`
(`r0_s1_runtime.c:3963-4002`). Canonical hosts wrap source as `[ ... ]`, so
the whole cell is one block.

```mermaid
flowchart LR
    S["source text"] --> H["host strips ;; line comments"]
    H --> W["host may wrap as one outer block"]
    W --> P["r0_s1_parse"]
    P --> B["loader block"]
    B --> R["r0_s1_run / r0_s1_run_persistent"]
```

---

## 2. Item and form alternatives

The block/program loop recognises items. Only `parse_form` recognises the
basic forms.

```mermaid
flowchart TD
    I["item special cases (top-level omits func)"] --> Q{"starts with double quote"}
    Q -->|"yes"| STR["string literal"]
    Q -->|"no"| K{"exact token func, inside a block"}
    K -->|"yes"| FUNC["func form"]
    K -->|"no"| M{"exact token masm or raw"}
    M -->|"yes"| MASM["masm form"]
    M -->|"no"| F["form"]
```

```mermaid
flowchart TD
    F["form (parse_form)"] --> B{"starts with open bracket"}
    B -->|"yes"| BLK["block"]
    B -->|"no"| N{"starts with digit, or - then digit"}
    N -->|"yes"| INT["integer"]
    N -->|"no"| W["word token (may be a word form)"]
```

The `func`, `masm` and `raw` tests happen before ordinary word parsing in
`parse_block` (`r0_s1_runtime.c:764-886`). `parse_program` recognises
`masm`/`raw` but **not** `func`; an unwrapped top-level `func` is an ordinary
word. A string at `form` position (for example a `func` spec) is a word token,
not a string literal.

---

## 3. Block

A block is a bracket-delimited sequence. Empty blocks are legal. Each string
literal expands to two forms.

```mermaid
flowchart TD
    A["open bracket"] --> B{"next non-space"}
    B -->|"close bracket"| Z["block value"]
    B -->|"end of input"| E["syntax error: unclosed block"]
    B -->|"string literal"| S["mk-string + byte block (2 forms)"]
    B -->|"func"| F["func form"]
    B -->|"masm or raw"| M["masm form"]
    B -->|"other form"| O["ordinary form"]
    S --> L{"form count reaches 512?"}
    F --> L
    M --> L
    O --> L
    L -->|"no"| B
    L -->|"yes"| X["parse failure: too_large"]
```

Form limit is 512 (`r0_s1_runtime.c:780`, `872`). A quoted string contributes
two forms even when empty.

---

## 4. Word token and word forms

The reader takes the longest run of non-whitespace bytes that excludes `[` and
`]`, truncates it to 63 bytes, then applies the first matching rule.

```mermaid
flowchart TD
    T["token (maximal run)"] --> TR["truncate to 63 bytes"]
    TR --> N{"exactly none?"}
    N -->|"yes"| NONE["NONE literal"]
    N -->|"no"| C{"last byte is colon?"}
    C -->|"yes"| SET["SET-WORD: name without final colon"]
    C -->|"no"| G{"first byte is colon?"}
    G -->|"yes"| GET["GET-WORD: name after colon"]
    G -->|"no"| L{"first byte is apostrophe?"}
    L -->|"yes"| LIT["LIT-WORD: name after apostrophe"]
    L -->|"no"| WORD["WORD"]
    WORD --> P{"name currently a lexical parameter?"}
    P -->|"yes"| BOUND["BOUND depth/slot fast path"]
    P -->|"no"| PLAIN["WORD symbol id"]
```

Order matters. `'x:` is a SET-WORD (trailing colon wins) whose name is `'x`.
`none` is the only special token; `none:`, `:none` and `'none` are word forms.

Evaluation of word forms:

```mermaid
flowchart LR
    S["SET-WORD"] --> AS["evaluate one expression; assign nearest binding or create local"]
    G["GET-WORD"] --> LK["lookup; return value without invoking"]
    L["LIT-WORD"] --> WO["return plain WORD value"]
    W["WORD/BOUND"] --> D["lookup or direct slot read; invoke callable result"]
```

---

## 5. Integer

```mermaid
flowchart LR
    A["optional -"] --> B["one or more decimal digits"]
    B --> C["INT payload"]
```

No `+` sign, no decimal point, no exponent, no hex, no underscores.

---

## 6. String literal

```mermaid
flowchart TD
    A["double quote"] --> B{"next byte"}
    B -->|"double quote"| Z["end literal"]
    B -->|"end of input"| E["syntax error: unterminated string"]
    B -->|"any byte, including backslash and newline"| C["append byte"]
    C --> D{"already 512 bytes?"}
    D -->|"yes"| X["parse failure: too_large"]
    D -->|"no"| B
    Z --> M["emit mk-string"]
    M --> BL["emit block of tagged byte integers"]
```

There is no escape syntax: `"` always ends the literal. The expansion is
`mk-string [b0 b1 ...]`, so the literal costs two forms and needs a
`mk-string` binding at run time.

---

## 7. `func` form

```mermaid
flowchart TD
    A["func (inside block)"] --> B["spec via parse_form (block, integer or word)"]
    B --> C{"is the next non-space byte an open bracket"}
    C -->|"yes"| D["parse literal body block under a fresh func site/scope"]
    C -->|"no"| E["leave the body expression in the enclosing form stream"]
    D --> F["emit func, spec, body"]
    E --> F
```

This `func` special case applies only inside a block. In an unwrapped
top-level source the word `func` is not special-cased. At run time `func`
evaluates the spec expression and the body expression. The body must evaluate
to a block. The spec block's element count is the arity.
Literal and computed bodies have different lexical capture rules
(`GLON-LANGUAGE-SPEC.md` §3.5).

---

## 8. `masm` / `raw` form

```mermaid
flowchart TD
    A["masm or raw"] --> B{"next non-space is a digit?"}
    B -->|"yes"| C["parse non-negative arity integer"]
    B -->|"no"| D["arity defaults to 0"]
    C --> E{"next non-space is an open bracket"}
    D --> E
    E -->|"no"| X["syntax error"]
    E -->|"yes"| F["tokenize private masm block"]
    F --> G{"tokens at most 511, labels at most 64, refs at most 128, code fits?"}
    G -->|"no"| Y["parse failure: too_large"]
    G -->|"yes"| H["record callable entry and arity for deferred emission"]
```

The `masm`/`raw` keyword must be the whole maximal token; `masm1` is an
ordinary word. The optional arity is recognised only as a non-negative decimal
integer after whitespace. The masm block is not Glon. Its mnemonics, operands
and labels are not interned and do not participate in Glon binding. The callable is a RAW value
with layout `[entry, arity]`.

---

## 9. Evaluation grouping (arity grammar)

There is no lexical grammar for grouping. The evaluator's arity determines
which following forms belong to a call.

```mermaid
flowchart TD
    E["expression stream"] --> S["evaluate one form (subexpression)"]
    S --> V{"result is callable?"}
    V -->|"native"| N["consume N arity expressions, each a subexpression"]
    V -->|"closure"| C["consume closure arity expressions, each a subexpression"]
    V -->|"RAW"| R["consume RAW arity expressions, each a subexpression"]
    V -->|"func keyword"| F["consume spec and body expressions"]
    V -->|"set-word"| A["consume one expression"]
    V -->|"return"| T["consume one expression, then unwind"]
    V -->|"other value"| X["the value is the result"]
    N --> O["evaluate call, leave result set"]
    C --> O
    R --> O
    F --> O
    A --> O
    T --> U["non-local return"]
    X --> O
```

Examples:

```glon
+ 1 * 2 3          ;; + 1 (* 2 3)      => 7
double double 5    ;; double (double 5)
f 1 2              ;; f consumes two expressions
values [10 20]     ;; one expression yielding two results
```

---

## 10. Parse trees for representative sources

```text
x: 5
+ x 3
```

```mermaid
flowchart TD
    P["program block"] --> S1["SET-WORD x"]
    P --> C["+ call"]
    S1 --> I["INT 5"]
    C --> W["WORD x"]
    C --> I3["INT 3"]
```

```text
double: func [x] [ * x 2 ]
double 21
```

```mermaid
flowchart TD
    P["program block"] --> S["SET-WORD double"]
    P --> C["call double"]
    S --> F["func form"]
    F --> SP["spec block x"]
    F --> B["body block * x 2"]
    C --> I["INT 21"]
```

```text
case [ [< n 10] [ 'small ] [1] [ 'large ] ]
```

```mermaid
flowchart TD
    C["case call"] --> A["cases block"]
    A --> R1["row 1: condition block and action block"]
    A --> R2["row 2: condition block and action block"]
    R1 --> C1["condition less than n 10"]
    R1 --> A1["action 'small"]
    R2 --> C2["condition 1 (catch-all)"]
    R2 --> A2["action 'large"]
```

`case` is ordinary library code. Its clauses are data blocks; it turns them
into zero-argument closures at run time.

---

## 11. Consolidated EBNF

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

Keyword alternatives apply only when the whole maximal token is exactly
`func`, `masm` or `raw`; `funcx` is a word. `func` is special-cased by
`parse_block`, not by `parse_program`, so an unwrapped top-level `func` is an
ordinary word. A quote at item start is a string; a quote inside `form`
(spec/body position) is part of a word token. The EBNF describes lexical
acceptance only. It does not describe arity grouping, function capture, result
sets or failure behaviour; those are in `GLON-EVALUATION-MODEL.md` and
`GLON-LANGUAGE-SPEC.md`.

---

## 12. Implemented lexical quirks worth remembering

| Quirk | Effect |
|---|---|
| Token truncation at 63 bytes | two long names can intern to one symbol |
| `[` first in source | only that block is parsed; trailing forms ignored |
| Unwrapped top-level `func` | `parse_program` does not assign a fresh func site/lexical scope; canonical hosts wrap source in one block |
| `;;` stripped by host, not reader | `;;` inside a string is also removed |
| String literal expands to `mk-string [...]` | needs library/prelude binding; costs 2 forms |
| No escapes | `"` cannot appear inside a string |
| `none` is a literal token | `none:` does not affect plain `none` |
| Trailing colon wins | `'x:` is a set-word named `'x` |
| No parentheses | grouping is entirely arity-driven |
| Single `;` is ordinary | only doubled `;;` is a host comment |


---

## 13. Diagram-to-EBNF map

| Diagram | EBNF production | Accuracy notes |
|---|---|---|
| §1 Source envelope | `source`, `trailing-tail` | A leading block ignores all trailing bytes; an unwrapped source is a sequence of top-items. |
| §2 Item alternatives | `top-item`, `block-item` | `func` is an item only inside a block; top-level `func` is a word. |
| §2 Form alternatives | `form` | `parse_form` accepts block, integer or word only; no string, `func` or `masm` special case. |
| §3 Block | `block`, `block-item` | Empty blocks accepted; 512-form limit; a string contributes two forms. |
| §4 Word forms | `word`, `token`, `word-value` | Token is maximal and truncated to 63 bytes before the first-match word rules. |
| §5 Integer | `integer` | Optional `-`, decimal digits only. |
| §6 String | `string` | Item-start only; no escapes; 512-byte limit; expands to `mk-string` plus a byte block. |
| §7 Func | `func-form` | Only inside a block; optional literal body block; computed body left in the stream. |
| §8 Masm | `masm-form` | Exact keyword token; optional non-negative arity; private masm block. |
| §9 Grouping | none | Grouping is an evaluation rule, not a lexical production. |
