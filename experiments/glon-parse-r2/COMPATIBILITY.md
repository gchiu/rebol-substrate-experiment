# Rebol 2 PARSE — Stage 1–2C compatibility matrix

## Oracle

- Executable: `rebol-core/rebol`
- `file`: ELF 64-bit LSB executable, x86-64, GNU/Linux
- `ldd`: uses the system `libm`/`libc` under WSL
- `system/version`: `2.7.8.4.10`
- Build date reported by the binary: 23-Jan-2016
- Product: `Core`

No Rebol 3 result was treated as Rebol 2 evidence. The oracle script is
`r2-oracle.r`; it uses textual R2 rules and the same inputs as the Glon suite.
`make oracle` runs it with `REBOL` (default `../../rebol-core/rebol`).

## Translation note

R2 distinguishes `parse string string` (simple splitting) from a **literal rule
inside a block**, e.g. `parse "Hello" ["hello"]`. The Glon Stage 1 API treats a
string passed as a rule as a literal, so the R2 oracle wraps every literal rule
in a block. This is an encoding/API difference, not a hidden host parser.

## Matrix

| Rebol 2 facility | Stage 1 status | R2 evidence / note |
|---|---|---|
| Complete match result | **PASS** | `p/parse`/`parse` return true only at end |
| Partial match position | **Glon API only** | `p/match` returns `[success pos]`; R2 `parse` has no position return. Prefix cases are checked with an extended rule (`B3prefix`). |
| Literal matching | **PASS** | R2 `parse "Hello" ["hello"]`; Glon `p/literal` |
| Default case-insensitivity | **PASS** | R2 A1/A2 true; Glon A1/A2 |
| `/case` | **PASS (API flag)** | R2 A3 false/A4 true; Glon `p/parse-case` |
| Sequence | **PASS** | R2 A5/A6 true; Glon |
| Default whitespace between patterns | **PASS** | R2 `"a b"`/tab true; leading whitespace true; trailing whitespace false |
| Leading whitespace | **PASS** | R2 `parse " a" ["a"]` true; Glon top skip |
| Trailing whitespace after literal | **PASS** | R2 `parse "a " ["a"]` false; Glon A10 false |
| `/all` exact spacing | **PASS (API)** | R2 A7 false/A8 true; Glon |
| Alternatives | **PASS** | R2 B4-B6 true, B7 false |
| Ordered alternatives / backtracking | **PASS** | R2 `["a"|"ab"] "c"` on `"abc"` false; Glon E6 false |
| Nested blocks | **PASS** | R2 C1 true |
| `some` | **PASS** | R2 C2/C3 true, C4 false |
| `some` with trailing whitespace | **PASS** | R2 `parse "a " [some "a"]` true; Glon E3 true |
| `any` | **PASS** | R2 C5/C6 true |
| `opt` | **PASS** | R2 C7/C8 true; R2 X1 (`opt ""` on `"x"`) false; Glon's C7/C8 cover opt |
| Named rule references | **PASS** | R2 C9-C11 true; flat encoded registry |
| Recursive named rules | **PASS** | R2 C11 true; Glon C11 |
| `some` zero-length | **PASS** | R2 D1 false; Glon D1 false (no infinite loop) |
| `any` zero-length on nonempty input | **PASS** | R2 D1b false; Glon E4 false |
| `any` zero-length on empty input | **PASS** | R2 D1c true; Glon E5 true |
| `some` zero-length on empty input | **PASS** | R2 X2 false; not a separate Glon assertion |
| Unknown named rule | **ERROR CLASS** | R2 raises a script/word error; Glon raises catchable SIN `unknown-rule` |
| `skip` | **PASS** | R2 F1-F6; Glon F1-F6 |
| `to` target | **PASS (literal target)** | R2 F9-F20; target left unconsumed; Glon |
| `thru` target | **PASS (literal target)** | R2 F8/F12/F14/F16/F19; target consumed |
| `end` | **PASS** | R2 F21-F23; Glon F21-F23 |
| Exact numeric repetition | **PASS** | R2 G1-G4/G8-G10/G16; Glon |
| Bounded numeric repetition | **PASS** | R2 G5-G7/G14-G15/G18; Glon |
| Charset member/range | **PASS (minimal pair descriptor)** | R2 H1-H13; Glon; matching is case-sensitive |
| Captures (`copy`/`set`) | **NOT IMPLEMENTED** | Stage 2D optional; not attempted |
| `none` rule value | **NOT IMPLEMENTED** | Outside Stage 1 |
| General `charset!`/`bitset!` datatype | **NOT IMPLEMENTED** | Stage 2C supports only the encoded pair descriptor |
| Textual Rebol rule syntax | **NOT IMPLEMENTED** | Rules are encoded Glon data; no host parser |
| `|` as a word token | **NOT IMPLEMENTED** | Encoded integer separator `0` |
| General `copy`/`set`, mark/restore | **NOT IMPLEMENTED** | Stage 2D optional; not attempted |
| Actions, block parsing, general R2 datatypes | **NOT IMPLEMENTED** | Outside Stage 2A–2C |
| Unicode/non-ASCII case | **NOT IMPLEMENTED** | Glon strings are byte strings |

## Test evidence

R2 was run locally under WSL (`rebol-core/rebol`, version `2.7.8.4.10`,
ELF x86-64). `make oracle` runs 96 probes: 42 Stage 1 and 54 Stage 2A–2C. The
full sequence includes true/false for literals, case, `/all`, whitespace,
alternatives, nested rules, repetition, named/recursive rules, navigation,
numeric repetition, charsets and zero-length boundaries.

`make test` runs nine fresh native S1-7 processes (A, B, C, D1, D2, E, F, G, H)
and reports **102 passing checks, 0 failures**. This is 93 encoded rule cases
(39 Stage 1 + 54 Stage 2A–2C); some include position or error assertions. The
groups are separate because the test data/code must fit the fixed 64K
machine—not because a host parses.

All comparable boolean outcomes match; the exceptions are inherently
non-boolean:
- B3 partial position: R2 has no position return; `B3prefix` checks the prefix.
- D2 unknown rule: R2 error; Glon catchable SIN.
- F7/F8/F11/F12 etc. are matched as complete parses or alternatives because R2
  does not expose consumed positions.
- X1/X2 add zero-length boundary probes beyond the Glon suite.

Stage 2 representative processes: F (2A) **2.33 s / 2.18 MB**, G (2B)
**0.16 s / 2.05 MB**, H (2C) **0.07 s / 2.05 MB** including CLI/library startup
(`/usr/bin/time -v`). These are smoke measurements, not parse benchmarks.

## Stage 2A–2C oracle notes

- `to` leaves the target unconsumed; `thru` consumes it. R2 `to "b" "b" "c"`
  succeeds; `to "z"` failure allows the next alternative to retry at the
  starting position.
- Numeric repetition skips intervening whitespace. Positive exact/bounded
  counts on zero-length rules fail; zero exact/bounded counts can succeed.
  Bounded repetition accepts fewer than max if the remaining input is consumed
  after the minimum is met.
- R2 charsets are **case-sensitive even without `/case`**; ranges are encoded
  here as `lo,hi` byte pairs. `charset!`/bitsets are not general R2 datatypes.
- Failures/positions are reported by the Glon `p/match` API; R2 has no position
  result, so prefix cases use an extended rule.

## Corrections made after R2 oracle

1. **A10** originally expected true; R2 returns false for trailing whitespace.
   Glon now does not consume trailing whitespace after a completed literal.
2. **E3** originally expected false; R2 returns true for `some` followed by
   trailing whitespace. Glon repetition now consumes skipped whitespace when it
   ends at input end.
3. **Zero-length repetition**: the first Glon version raised a SIN/stopped
   arbitrarily. R2 returns false for `some ""` and for `any ""` on nonempty
   input, and true for `any ""` at end. Glon now follows those outcomes.
4. **Leading/trailing whitespace**: R2 accepts leading whitespace but rejects
   trailing whitespace after a literal. Glon was corrected accordingly.
5. **Alternatives**: R2's no-re-entry behaviour was verified; Glon's ordered
   choice matches on the tested case.

No remaining boolean discrepancy was found. Unimplemented operators and the
textual rule dialect remain the Stage 2 boundary, not a Stage 1 discrepancy.
