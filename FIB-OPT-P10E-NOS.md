# FIB-OPT-P10E — Two-cell (TOS + NOS) data-stack caching

## 1. Hypothesis

P10D cached the top stack item (`tos`) and gave ~1.32×. P10E tests whether
caching the second item (`nos`, next-on-stack) too yields a further meaningful
gain, by mechanically extending the P10D lowering to two cached locals.

## 2. Invariant and stack-pointer convention

Data stack grows down. P10E caches the top two values:

- `tos` = logical top.
- `nos` = logical second (next-on-stack).
- `sp` = address of the **third**-from-top (`M[sp]` is the third).
- The top slot in memory is `sp - 2`, the second slot is `sp - 1`.

Initial state (empty stack): `sp = M[1]` (DS_INIT), `tos = 0`, `nos = 0` (both
dummy; the first push spills the dummies, which the balanced S1 discipline never
reads as live values).

| op | P10D | P10E |
|---|---|---|
| push v | `M[--sp]=tos; tos=v;` | `M[--sp]=nos; nos=tos; tos=v;` |
| DUP | `M[--sp]=tos;` | `M[--sp]=nos; nos=tos;` |
| DROP | `tos=M[sp++]` | `tos=nos; nos=M[sp++]` |
| `@` | `tos=M[tos]` | `tos=M[tos]` |
| `!` | `{a=tos;v=M[sp];tos=M[sp+1];sp+=2;M[a]=v;}` | `{a=tos;v=nos;tos=M[sp];nos=M[sp+1];sp+=2;M[a]=v;}` |
| 0BRANCH | `{f=tos;tos=M[sp++];ip=…}` | `{f=tos;tos=nos;nos=M[sp++];ip=…}` |
| ADD…GE | `{b=tos;a=M[sp];sp+=1;tos=a⊕b;}` | `{b=tos;a=nos;nos=M[sp++];tos=a⊕b;}` |
| NEG | `tos=-tos` | `tos=-tos` |

## 3. Register-access idiom (SP now virtualised over two cells)

`IP`/`RP` remain direct locals. `SP` (`M[1]`) is virtualised as `tos`+`nos`+`sp`,
so the SP idioms become:

- `LIT 1 @ @` (e_peek, = DUP) → `M[--sp]=nos; nos=tos;`
- `LIT 1 @` (read SP) → `{_s=sp-2; M[--sp]=nos; nos=tos; tos=_s;}` (logical SP =
  `sp - 2`)
- `LIT 1 !` (write SP) → `{_v=tos; tos=M[_v]; nos=M[_v+1]; sp=_v+2;}`

`LIT 0/2 @/!` (IP/RP) push/pop through the two-cell push/pop idiom.

## 4. Flush / reload

- Flush = `M[--sp]=nos; M[--sp]=tos;` (spills nos then tos; `sp` now points at
  the top, canonical). Reload = `tos=M[sp++]; nos=M[sp++];`.
- At generic `HOST` (`ALLOC`/`PUTCHAR`/`PRINT`), `HOST_DUMP`, `HALT`, `L_bad`.
- Pure inline HOST and the register idioms need no boundary.

## 5. Correctness safeguards

Every lowering is the P10D transcription extended to the second cached cell;
`cell` arithmetic (signedness/overflow/division truncation) is unchanged; the
SP idioms are derived from `tos`=top, `nos`=second, `sp-2`=top slot. No
S1/Glon/RAW/frame/task/debugger/GC semantics change; the frozen stream is
untouched.
