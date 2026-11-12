# B13 results — selectable S1-7 / S1-12 profiles

## 1. Profile selection

Selection is **build-time and executable-based**, with no change to application
source, Glon syntax, the `glon` command or the production default:

```
make -C experiments/glon-profiles-b13
experiments/glon-profiles-b13/build/glon-s1-7  <same Glon arguments>
experiments/glon-profiles-b13/build/glon-s1-12 <same Glon arguments>
```

`profile.sh s1-7|s1-12 ...` is a convenience wrapper. Both executables link the
same `glon.c`, `r0_s1_session.c` and `r0_s1_show.c`; S1-7 uses the frozen
`r0_s1_runtime.c` + `s1.c`, while S1-12 uses the existing B12 runtime and
machine copies with `B12_NATIVE_RS=1`. They share `build/glon-lib` (`prelude`,
`strings`). The default repository `./glon` is untouched.

The profile is not selected by a runtime flag or profile-specific syntax. That
keeps the integration bounded: no application source changes and no production
runtime switch. S1-12 is native-only; B12 has no WASM port.

## 2. Supported targets

- **Native CLI:** S1-7 and S1-12 both work.
- **WASM/browser:** S1-7 remains the existing shop WASM host; S1-12 has no WASM
  build. Browser applications are therefore outside B13's native profile test.
- **Production default:** unchanged S1-7; S1-12 is experimental.

## 3. Application compatibility

| Application | S1-7 | S1-12 | Evidence and boundary |
|---|---|---|---|
| FizzBuzz | PASS | PASS | 100/100 lines; expected-output and profile outputs are byte-identical |
| LaTeX v001 tests | PASS | PASS | existing `latex.glon + test-helpers + latex-tests`; output identical |
| LaTeX v002 tests | PASS | PASS | existing `chris-samples + latex-v002-tests`; output identical |
| DEFLATE | PASS | PASS | B12 evidence reused: 480/480 conformance, 44/44 units, byte-identical |
| Glon shop/browser apps | PASS (existing WASM host) | **Not available** | `app.glon`, shop demos, primer and traffic need the DOM/event/WASM host; native CLI does not provide it |

The FizzBuzz source is the existing local FizzBuzz worktree fixture, not a copy
or substitute. `test.sh` uses `FIZZBUZZ_DIR` (default
`/tmp/opencode/glon-fizzbuzz/experiments/glon-fizzbuzz`); if the worktree is
absent it reports the skip explicitly. LaTeX uses the actual repository test
sources.

The shop boundary is genuine integration, not a failed native program: the shop
apps depend on `host_set_html`, DOM events and `glon-live`/WASM loading. B13
does not invent a substitute native application. Existing shop tests remain
browser/WASM tests and were not rerun.

## 4. Correctness evidence

```
bash experiments/glon-profiles-b13/test.sh
fizzbuzz: PASS (100/100, profiles identical)
latex: PASS (both suites, profiles identical)
OK: S1 substrate frozen at f90496c...
b13: PASS
```

FizzBuzz and LaTeX outputs are directly compared between S1-7 and S1-12; both
programs use the same source and libraries. DEFLATE's full suites are the B12
results (`a202645`); B13 changes no DEFLATE/runtime implementation, so they are
reused rather than rerun. Frozen-S1 passes; no tests were weakened.

## 5. Performance

The meaningful whole-application result remains B12's whole-DEFLATE comparison
(S1-7 vs S1-12, O2, five repetitions):

| Workload | S1-7 | S1-12 | Speedup |
|---|---:|---:|---:|
| fixed | 978.0 ms | 825.8 ms | 1.184× |
| dynamic | 4,226.8 ms | 3,910.1 ms | 1.081× |
| streaming | 6,308.5 ms | 5,589.8 ms | 1.129× |

Those measurements are not repeated here; B12's `baseline/b12-results.json`
and `B12-RESULTS.md` are the source of record.

Small-app timings (5 runs, medians, process startup included; not a meaningful
optimisation benchmark) are preserved in `baseline-apps.json`:

| Program | S1-7 | S1-12 |
|---|---:|---:|
| FizzBuzz | 31.7 ms | 24.0 ms |
| LaTeX v001 tests | 150.9 ms | 132.2 ms |
| LaTeX v002 tests | 291.6 ms | 233.3 ms |

The tiny-app differences are dominated by fixed startup/loading and are not
used to claim general application speedup.

## 6. Implementation and maintenance cost

B13 adds only integration/validation files:

- `Makefile`, `profile.sh`, `test.sh`, `measure_apps.py`, README/results;
- two ~111 KB experimental CLI executables sharing the same libraries;
- no new C runtime, no duplicated application source, no compiler change.

The actual S1-12 implementation is B12's machine/runtime copy (about 4,553 C
lines), which must track the generated runtime. B13 does not reduce that
maintenance obligation; it makes the existing experimental profile selectable.

## 7. Limitations

- Native-only. No S1-12 WASM/browser host.
- Shop applications are not native-CLI applications; their WASM/DOM tests are
  outside this comparison.
- FizzBuzz depends on the separate local FizzBuzz worktree.
- S1-12 remains an experimental runtime copy; B12's raw-masm size accounting
  and native opcodes are not production changes.
- The native CLI comparison covers FizzBuzz, LaTeX and DEFLATE; it does not
  establish general portability or production readiness.
- No deployment, push or default-profile change.

## 8. Recommendation

**Retain S1-12 as an experimental native execution profile.** The selection
mechanism is simple, reproducible and does not disturb `glon`; existing native
FizzBuzz, LaTeX and DEFLATE applications run unchanged, with equal output and
the measured DEFLATE benefit. Do not promote it to production default yet: the
missing WASM/browser profile and the shop host boundary are real limitations,
and B12's runtime copy is still an experimental maintenance surface.

The next step, if desired, is an isolated WASM/browser profile validation
against the same applications—not a compiler expansion and not another native
DEFLATE experiment.
