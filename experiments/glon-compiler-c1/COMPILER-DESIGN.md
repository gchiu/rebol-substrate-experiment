# COMPILER-DESIGN.md — minimum vertical slice

## Scope

C1 extends a **copy** of the B3 textual compiler; B1–B3 are untouched. The
target machine is the B3 experimental machine with the twelve-operation
candidate flags:

```
CALL | RET | >R | R> | R@
```

No BRANCH native is used. No VM opcode, frozen S1 file, production runtime,
host ABI or existing DEFLATE source was changed.

## Compiler additions

1. **Function-only modules.** The B3 parser required a top-level expression.
   C1 permits a module containing only definitions and selects a named function
   as the entry. `c1_compile(source, "hf/left", flags, ...)` returns that
   function's entry. No module wrapper is supported.
2. **`block-at` intrinsic.** The parser recognises `block-at` at arity 2; the
   emitter produces:

   ```
   <block handle>          raw base address, from the ABI
   <index>                 tagged integer
   LIT 16; HOST DIV        untag index
   HOST ADD                base + index
   FETCH                   tagged element
   ```

   This is a compiler lowering using existing S1 operations, not a new VM
   primitive.
3. **Function-only driver ABI.** After compilation, the C harness appends a
   trampoline at the current code cursor:
   - candidate: native `CALL` + `HALT` (3 cells);
   - seven-op: existing macro `CALL` + `HALT` (22 cells).

   The trampoline is harness code, not part of the unchanged function source.
   Cell totals report both the function-only code and total code including the
   trampoline.

No set-word/local binding, division, string, block literal, closure, `do`,
`judge`, `invoke`, `s/`, masm or module wrapper support was added.

## Kernel ABI

| Item | Representation |
|---|---|
| Function | unchanged `hf/left` body |
| Parameters | convention A: caller pushes `c`, `l`, `left`; callee saves on return stack; `c` read with native R@, others via RP offset |
| Block handle | raw base cell supplied by harness; not a full Glon block header |
| Block elements | tagged integers in fixed `M` cells |
| `block-at` | raw base + untagged index + fetch; **no bounds check** |
| Return value | tagged integer (`left`/remainder or -1) |
| Call/return | candidate native CALL/RET |
| Stack | frozen data/return stacks; no new registers or memory |
| GC | none; block storage is a fixed harness allocation at `M[40000..]` |
| Errors | none beyond S1 invalid opcode; no SIN! support |

The block handle representation is an intentional harness-only ABI for this
kernel, not a claim that general Glon BLOCK! values are compiled. The original
production `block-at` is a masm/preamble service over a real block header; C1's
intrinsic indexes the harness's fixed array. The kernel's observable results
match because `hf/left` only reads the supplied count elements.

## Memory and code accounting

- Function code: 157 cells candidate, 279 cells seven-op.
- Total with the call/halt trampoline: 160 vs 301 cells.
- Block allocation: 16 tagged count cells at a fixed address in the existing
  `M[65536]`.
- Stack: max data 6 cells, return 64 cells in these vectors; no heap, loader or
  GC allocation.
- Compiler object: 9,227 text bytes / 256 bss, versus B3's 8,553 text bytes;
  source 602 lines versus 573. The machine is unchanged B3 (5,505 text bytes;
  fixed 524,328 bss).
- C1 compiler source is 602 `.c` + 11 `.h` lines; harness 141 lines.

## What the slice proves and does not prove

It proves that **one genuine, unchanged DEFLATE function**—including recursion,
three arguments, two `block-at` reads and the five candidate control
operations—can run on the twelve-operation machine without a new primitive.

It does not prove general block semantics, locals, strings, closures, errors,
GC, LZ77 or the complete decoder. Those require the work listed in
`DEFLATE-COVERAGE.md`.
