# Rebol 2 PARSE — Stage 1 compatibility matrix

Reference: REBOL/Core Users Guide, chapter 15 “Parsing”
(<https://www.rebol.com/docs/core23/rebolcore-15.html>), read 2026-11-12.
No Rebol 2 executable was available in this environment. Rebol 3 was not used
as an oracle; its semantics differ from Rebol 2.

| Rebol 2 facility | Stage 1 status | Evidence / note |
|---|---|---|
| `parse series rules` complete-match result | **PASS** | `p/parse` returns 1 only when the whole string is consumed; trailing input returns 0 |
| Partial match observation | **PASS (Glon API)** | `p/match` returns `[success position]`; this is not a Rebol API addition |
| Literal string matching | **PASS** | `parse.glon` `p/literal`/`p/eq` |
| Default case-insensitive strings | **PASS** | ASCII folding; docs §1 |
| `/case` behavior | **PASS (API flag)** | `p/parse-case`; `/all` + case mode not combined in the public API |
| Sequence rules | **PASS** | `p/seq` |
| Default whitespace between patterns | **PASS** | `p/skip-ws`, docs §8 |
| `/all` exact spacing | **PASS (API)** | `p/parse-all`; docs §1/§8 |
| Alternatives `|` | **PASS** | Encoded as integer `0` between branches; `p/alt` |
| Ordered alternatives | **PASS, choice documented** | First successful branch wins; engine does not re-enter a successful branch if an outer rule later fails. A controlled R2 oracle is still needed to settle backtracking equivalence. |
| Nested rule blocks | **PASS** | `p/block`/`p/rule` |
| `some` | **PASS** | `[ 1 rule ]`; zero-length rule raises a catchable `parse zero-length` |
| `any` | **PASS** | `[ 2 rule ]`; zero-length progress stops safely |
| `opt` | **PASS** | `[ 3 rule ]` |
| Named rule references | **PASS** | Flat registry and `p/lookup`; recursive named rules tested |
| `end` rule value | **NOT IMPLEMENTED** | Explicitly outside Stage 1 |
| `none` rule value | **NOT IMPLEMENTED** | Explicitly outside Stage 1 |
| Textual Rebol rule syntax | **NOT IMPLEMENTED** | Rules are encoded Glon data; no host parser |
| `|` as a word token | **NOT IMPLEMENTED** | Encoded integer separator instead |
| Character sets / bitsets | **NOT IMPLEMENTED** | Stage 2 |
| `skip`, `to`, `thru` | **NOT IMPLEMENTED** | Stage 2 |
| `copy`, `set`, mark/restore | **NOT IMPLEMENTED** | Stage 2 |
| Numeric repetition | **NOT IMPLEMENTED** | Stage 2 |
| Parse expressions/actions | **NOT IMPLEMENTED** | Stage 2 |
| Block parsing / datatype rules | **NOT IMPLEMENTED** | Outside Stage 1 |
| Unicode/case beyond ASCII | **NOT IMPLEMENTED** | Glon strings are byte strings |

## Test evidence

`make test` runs five fresh native S1-7 processes (test groups A, B, C, D1, D2)
for **40 passing checks, 0 failures**. The groups are separate because the
test data/code must fit the fixed 64K machine; this is a test-harness constraint,
not a host parser.

Representative cases: complete and partial matches; trailing input; sequence
failure; first/second/ordered alternatives; nested and recursive named rules;
`some`/`any`/`opt`; default whitespace and `/all`; default case-insensitive and
`/case`; zero-length repetition guard; unknown named rule error.

One A-group native process measured about **0.07 s** and **1.9 MB maximum RSS**
(`/usr/bin/time -v`), including Glon CLI/library startup. This is a smoke
measurement, not a parse benchmark.

## Deviations / unresolved

1. Rules are encoded data, not Rebol source syntax.
2. Alternatives are ordered with no re-entry after a successful branch; R2
   backtracking equivalence needs an actual R2 oracle.
3. Zero-length `some` raises a Glon `parse zero-length` guard; `any` stops at
   the first non-progress iteration. R2's exact infinite-repetition behavior was
   not observable without an R2 interpreter.
4. Case folding is ASCII only.
5. Unknown named rules raise a catchable SIN; error text/identity is not a Rebol
   equivalence claim.
