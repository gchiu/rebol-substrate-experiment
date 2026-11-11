# B11 — native call/return in the existing Glon evaluator

Experimental evaluator profile for the existing seven-primitive Glon runtime.
The native instructions are B9’s CALL, RET, >R, R> and R@; the original
`r0_s1_runtime.c`, `s1.c`, Glon sources and DEFLATE files are unchanged.

`b11_runtime.c` is a copy of the runtime emitter with only the native
call/return forms and the matching raw-masm size accounting. `b11_machine.c`
is the tested B3/B9 machine copy with native `asm_call`, `asm_exit`,
`asm_toR`, `asm_fromR` and `asm_fetchR`; the seven-primitive control is a
fresh O2 `dzhost` build.

## Build and verify

```
make -C ../glon-deflate build/dzhost
make all
bash test.sh
```

`test.sh` runs the complete 480/480 conformance suite, the 44/44 unit suite
through the B11 CLI, and the frozen-S1 check. `make bench` regenerates the
fair O2 seven-versus-B11 measurements in `baseline/b11-results.json`.

The normal b11 host is `build/dzhost-b11`; `build/dzhost-b11-count` adds the
B11 native-op counters; `build/glon-b11` is the CLI for unit tests.

## Scope

This is a bounded experimental evaluator, not a compiler or a production
runtime change. It does not add Glon syntax, change DEFLATE, alter heap/GC, or
modify the deployed browser demonstration.
