# Glon PARSE R2 — Stage 2A–2D

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

Stage 2 adds encoded operator blocks (used inside sequences; a sequence
needs a nested block for each operator):

| Glon data | Meaning |
|---|---|
| `[ 4 ]` | `skip` one byte |
| `[ 5 "target" ]` | `to` literal target; leaves it unconsumed |
| `[ 6 "target" ]` | `thru` literal target; consumes it |
| `[ 7 ]` | `end` |
| `[ 8 descriptor ]` | charset; descriptor is a byte string of `[lo hi]` pairs |
| `[ 20 n rule ]` | exact numeric repetition |
| `[ 21 min max rule ]` | bounded numeric repetition |
| `[ 9 'name rule ]` | `copy` capture (Stage 2D) |
| `[ 10 'name rule ]` | `set` marker (unsupported in R2 string parse) |

Stage 2 charset matching is case-sensitive, matching the R2 oracle. `to`/`thru`
currently require a literal string target, not an arbitrary rule. Numeric
repetition skips intervening whitespace; zero-length rules cannot satisfy a
positive count. `copy` stores a source slice in the Glon capture log; `set` is
rejected because Rebol 2.7.8 does not support it in string rules (it is a block
parse operation).

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
the 64K machine. It consists of **133 checks across twelve groups** (`A`–`E`
Stage 1; `F1`/`F2` Stage 2A; `G` Stage 2B; `H` Stage 2C; `I1`/`I2` Stage 2D).

## Compatibility status

Stage 2A–2D are implemented to the verified extent and all **133/133 local
checks** pass. Representative native process measurements including CLI/library
startup (`/usr/bin/time -v`): Stage 2A F groups **2.33 s / 2.18 MB**, Stage 2B G
**0.16 s / 2.05 MB**, Stage 2C H **0.07 s / 2.05 MB**, Stage 2D I1 **0.13 s /
2.18 MB**, I2 **0.08 s / 2.05 MB**. These are smoke measurements, not parse
benchmarks.

- literal matching, default ASCII case-insensitivity and `/case`;
- sequences, ordered alternatives, nested blocks;
- `some`, `any`, `opt`;
- named rule references and recursive named rules;
- complete success, partial/trailing input, failure positions;
- default whitespace skipping and `/all`;
- zero-length repetition guards;
- `skip`, `to`, `thru`, `end`;
- exact/bounded numeric repetition and charset ranges;
- charset case sensitivity and charset repetition.

Rebol 2 reference: the REBOL/Core Users Guide, chapter 15 “Parsing”
(<https://www.rebol.com/docs/core23/rebolcore-15.html>), plus the genuine
REBOL/Core **2.7.8.4.10** Linux x86-64 binary (`rebol-core/rebol`) under WSL.
`make oracle` runs `r2-oracle.r` (42 Stage 1 probes),
`r2-stage2-oracle.r` (54 Stage 2A–2C probes) and `r2-capture-oracle.r` (16
Stage 2D probes). All comparable boolean outcomes match; capture values also
match except for the deliberately unsupported `set` form, where R2 raises an
error and Glon raises `unsupported-set`. Rebol 3 was not used as an R2 oracle.

Stage 2A–2C implement `skip`, `to`, `thru`, `end`, exact/bounded numeric
repetition, and minimal range/member charsets. Stage 2D implements `copy` with
a Glon capture log; `set` is unsupported because R2 2.7.8 rejects it in string
rules. Still not implemented: textual Rebol rule syntax, `|` as a word, `none`,
block parsing, general charsets/bitsets beyond the minimal pair descriptor,
general R2 actions, and full `/all`/`/case` surface syntax. `p/parse` is the Glon
API, not a native `parse` primitive.

R2-verified semantics: alternatives are ordered with no re-entry after a
successful branch (`E6` matches R2). Leading whitespace is accepted; trailing
whitespace after a literal is rejected. `some` can consume trailing whitespace
when it ends at input end. `some ""` is false; `any ""` is false on nonempty
input and true at end. These outcomes match the R2 binary and avoid infinite
repetition. The textual rule dialect and the unimplemented operators remain
Stage 2 work, not claimed compatibility.

## Proof of execution

`parse.glon` contains the matching, sequence/alternative control flow,
repetition and named-rule lookup. `parse-tests-*.glon` are Glon data/programs;
there is no host parser, regex, PEG library, or precomputed result. The only
masm helper is the safe value-tag read used to distinguish strings/blocks from
rule words.

The full matrix, oracle translation note, and corrected deviations are in
`COMPATIBILITY.md`.

## Limits / next obstacle

Stage 2A–2D are an encoded, bounded subset, not full Rebol 2. The next
compatibility obstacle is the textual rule dialect/parser, general actions and
block parsing; those should be introduced incrementally with the same data
format and R2 oracle comparisons, without adding VM features. `set` in string
parsing has no R2 semantics to implement; it remains an explicit unsupported
case. No Stage 3 was started.
