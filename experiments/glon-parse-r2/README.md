# Glon PARSE R2 — Stage 1

A bounded Rebol 2 `parse` rule engine written entirely in Glon and running on
the frozen seven-primitive S1-7 CLI. The host only compiles/loads Glon sources
and captures output; it does not parse or match input.

## Rule representation

No textual Rebol rule parser is included. Rules are ordinary Glon DATA built
with `reduce` (literal Glon blocks are code, so strings must be produced by
`reduce`). The encoding is:

| Glon data | Meaning |
|---|---|
| `"literal"` | literal string; case-insensitive by default |
| `[ ... ]` | sequence |
| `[ a 0 b ]` | ordered alternatives (`0` is the separator) |
| `[ 1 rule ]` | `some` (one or more) |
| `[ 2 rule ]` | `any` (zero or more) |
| `[ 3 rule ]` | `opt` (zero or one) |
| `word` | named rule in the flat registry |
| `end` / `none` | not implemented in Stage 1 |

The registry is a flat data block `[ name rule name rule ... ]`. Example:

```
digit: reduce [ "0" 0 "1" 0 "2" 0 "3" 0 "4" 0 "5" 0 "6" 0 "7" 0 "8" 0 "9" ]
digits: reduce [ 1 'digit ]
rules: reduce [ 'digit digit 'digits digits ]
x: p/parse rules "101" 'digits
```

`p/parse` (default), `p/parse-all`, and `p/parse-case` return `1`/`0` only when
the whole input is consumed. `p/match*` returns `[success position]` so partial
matches and trailing input can be observed. The default mode skips intervening
ASCII whitespace between patterns, as documented for Rebol 2; `/all` does not.
Case-insensitive literal matching is the default; `p/parse-case` enables ASCII
case sensitivity.

## Commands

```
make
make test
# or
bash test.sh
```

The suite runs in separate native processes because the test data/code must fit
the 64K machine. It consists of 40 checks across five groups (literals, sequence,
alternatives, nesting, repetition, named records, guards).

## Compatibility status

Stage 1 passes 40/40 local checks and all 5 test processes complete. One
A-group native process measured about 0.07 s and 1.9 MB maximum RSS including
Glon CLI/library startup (`/usr/bin/time -v`); this is a smoke measurement, not
a parse benchmark. It covers:

- literal matching, default ASCII case-insensitivity and `/case`;
- sequences, ordered alternatives, nested blocks;
- `some`, `any`, `opt`;
- named rule references and recursive named rules;
- complete success, partial/trailing input, failure positions;
- default whitespace skipping and `/all`;
- zero-length repetition guards.

Rebol 2 reference: the REBOL/Core Users Guide, chapter 15 “Parsing”
(<https://www.rebol.com/docs/core23/rebolcore-15.html>). No Rebol 2 executable
was available in this environment; the installed Rebol 3 binary is not
semantically authoritative for R2 and was not used as an oracle.

Not implemented in Stage 1: textual Rebol rule syntax, `|` as a word, `end`,
`none`, charsets/bitsets, `skip`, `to`, `thru`, `copy`, `set`, numeric
repetition, block parsing, parse expressions/actions, and full `/all`/`/case`
surface syntax. `p/parse` is the Glon API, not a native `parse` primitive.

Compatibility choice: alternatives are ordered; after a branch succeeds the
engine does not re-enter that choice if a later outer rule fails. This is
documented pending a controlled Rebol 2 oracle comparison. `some` of a
zero-length rule raises a catchable `parse zero-length` SIN; `any` stops at the
first non-progress step. These guards avoid an infinite loop and are explicit
deviations/guards rather than silent PEG semantics.

## Proof of execution

`parse.glon` contains the matching, sequence/alternative control flow,
repetition and named-rule lookup. `parse-tests-*.glon` are Glon data/programs;
there is no host parser, regex, PEG library, or precomputed result. The only
masm helper is the safe value-tag read used to distinguish strings/blocks from
rule words.

The full matrix and deviation notes are in `COMPATIBILITY.md`.

## Limits / next obstacle

Stage 1 is an encoded, bounded subset, not full Rebol 2. The next compatibility
obstacle is the **textual rule dialect/parser** and then the full set of parse
operators (charsets, `skip`/`to`/`thru`, `copy`/`set`, numeric repetition and
actions). Those should be introduced incrementally with the same data format
and R2 oracle comparisons; do not add VM features for them.
