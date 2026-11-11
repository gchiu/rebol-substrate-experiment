# DEFLATE-COVERAGE.md — language requirements and implementation needs

## What `hf/left` uses

| Construct | Use in kernel | C1 treatment |
|---|---|---|
| Function definition | `hf/left: func [c l left] [...]` | existing B3 parse/codegen |
| Parameters | `c`, `l`, `left` | existing convention A lowering |
| Integer literals | `0`, `1`, `2`, `15`, `-1` | existing |
| Arithmetic | `*`, `-` | existing tagged lowering |
| Comparisons | `<`, `>` | existing |
| `either` | two conditionals | existing |
| Recursion | `hf/left` calls itself | existing direct CALL |
| `block-at` | `block-at c l` twice | **new compiler intrinsic** using existing primitives |
| Block value | `c` parameter | **harness ABI:** raw block base cell; no block literal/header |

It does **not** use set-words, locals, division, strings, closures, `do`,
`judge`, `invoke`, `s/`, masm or module wrappers.

### `block-at` lowering

`block-at` is compiled directly, without a new VM opcode:

```
compile block handle        ; raw base address cell (harness supplied)
compile index               ; tagged integer
LIT 16
HOST DIV                    ; untag index
HOST ADD                    ; base + raw index
@                           ; fetch tagged block element
```

This matches the arithmetic block-at access used by the production prelude for
the selected indices. C1 does not implement full Glon block headers, block
literals, bounds checks, growth, GC or block mutation.

## Full-decoder constructs and classification

| Construct / area | Actual use | Implementation need | C1 status |
|---|---|---|---|
| Module `[ ... ]` wrappers | prelude/strings/DEFLATE/traffic/kaka sources | compiler/loader work | unsupported |
| Set-words / local bindings | `hf/walk`, `hf/build`, `dz/dynamic`, traffic, kaka | lexical scope + frame/local slots; compiler work | unsupported |
| Division `/` | `hf/walk`, `hf/syms`, `hf/build`, traffic fixed-point | tagged integer division lowering using existing HOST DIV; compiler work | unsupported |
| Block values | Huffman count tables, decoder closures | runtime representation/ABI; C1 only supplies a raw indexable handle | partial, harness-only |
| Strings and byte access | input, Huffman symbol bytes, LZ77 output | managed string runtime and GC integration | unsupported |
| Closures / closure blocks | bit reader, decoder state, `dz/loop` | capture/context representation and invocation | unsupported |
| `do` | unit-test and decoder helper groups | evaluation/frame lowering | unsupported |
| `judge` / SIN! errors | decoder error paths | error representation/non-local return | unsupported |
| `invoke` / `block-at` on closures | bit reader, decoder | closure invocation ABI | unsupported |
| `s/` string operations | input/window/LZ77 | strings and byte primitives/compiler work | unsupported |
| masm | prelude/string/window helpers | existing production assembler; C1 compiler has no masm parser | unsupported |
| Host DOM/canvas/events | traffic, Kākā | host/runtime integration | unsupported |
| Bounds checks | all block/string accesses | runtime checks; K1 remains open | not implemented |
| GC / ownership | DEFLATE window/decoder | managed heap/GC integration | not implemented |

## Classification conclusion

- **Existing compiler lowering:** functions, parameters, recursion,
  arithmetic/comparisons, `either`, calls.
- **New compiler lowering using existing VM operations:** `block-at` and
  function-only module entry selection.
- **Runtime representation/ABI:** raw block handle and tagged elements; this is
  harness-only, not a general block runtime.
- **Host/runtime integration:** strings, closures, GC, errors and browser hosts
  are required for the full decoder, but not for `hf/left`.
- **New VM primitive:** none demonstrated. `block-at` uses existing S1 `LIT`,
  `HOST DIV`, `HOST ADD` and `@`; the candidate's five control operations are
  sufficient for the extracted kernel.

The complete decoder remains blocked primarily by compiler/runtime coverage,
not by a missing VM opcode.
