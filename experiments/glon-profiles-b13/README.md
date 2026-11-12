# B13 — selectable Glon execution profiles

Build-time profile selection for the existing native Glon CLI. The default
`./glon` is untouched; these are separate experimental executables.

- `build/glon-s1-7` — frozen seven-primitive `s1.c` and runtime.
- `build/glon-s1-12` — B12's native `CALL`, `RET`, `>R`, `R>`, `R@` machine and
  runtime emitter.
- `profile.sh s1-7|s1-12 ...` — convenience wrapper.
The shop/browser applications are not native-CLI applications; they use the
WASM/DOM host and are documented as an unsupported boundary rather than
replaced by a substitute native program.

Build and validate:

```
make
bash test.sh
```

`test.sh` runs FizzBuzz and both LaTeX suites under both profiles, checks
output equality, and runs the frozen-S1 check. The FizzBuzz source is the
existing local worktree fixture (set `FIZZBUZZ_DIR` if it is elsewhere); it is
not duplicated in B13.

Both executables accept the same Glon source and use the same `build/glon-lib`.
The profile is selected by executable, not by Glon syntax or a runtime flag.
Browser/WASM hosts are not replaced; B12's native profile has no WASM port.
No production, frozen-S1, DEFLATE, heap or GC source is changed.
