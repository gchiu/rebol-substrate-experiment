# GLON-VOCAB-1: genuine vocabularies and lexical binding

Status: 2026-11-27, branch `glon-gui-f2`, after GLON-LANG-1.
- Frozen S1 (`s1.c`, `s1.h`, `tests.c`, `adversarial.c`, `claims.c`) is
  unchanged; `check-frozen-s1.sh` passes.
- The S1 memory layout, code boundaries and the emitted runtime are
  unchanged: the runtime still emits 12,884 cells, and Intent sessions still
  have 58 code cells free.
- Everything new is in the loader (the C parser in `r0_s1_runtime.c`, which
  emits no code), plus the optional `set` word.
- Probes and logs are in `../diagnostics/vocab1-*.log` and
  `../diagnostics/lang1-probes/`.

**In short:**
- **Function locals no longer depend on which globals exist.**
  - A set-word in a function makes a local of that function, or updates a
    variable of an enclosing function, decided from the source when it is
    loaded.
  - Globals defined later, by another source or at run time, can never be
    reached.
  - An application global `k` or `face` no longer changes any library
    function: the GLON-LANG-1 collision is gone, shown by a test that fails on
    the old runtime.
- **`vocabulary NAME [ ... ]` declares a real scope.**
  - Members are its top-level definitions, used inside by their plain names.
  - Vocabularies nest (`geo/point/sum`, `intent/window/instance`), and member
    names are reused freely across vocabularies.
  - Unknown members, definitions outside the vocabulary and duplicates are
    load errors.
- **The migrations:**
  - `s/` is a vocabulary, across the three string files;
  - `n/` is a new numbers vocabulary written natively;
  - Intent's window library is `vocabulary intent [ ... ]`, with a nested
    `intent/window` vocabulary.
- **Explicit outer access:**
  - an enclosing function's variable, by its plain name;
  - a vocabulary member, by its qualified name;
  - a global, with `set 'name value`;
  - a global hidden by a member, read as `global/name`;
  - a local, for inspection, as `'local/name`.
- **Cost:**
  - globals and function sites unchanged (+1 global, a renamed function);
  - code cells unchanged (+67 only if `binding.glon` is loaded, for `set`
    with a computed name);
  - managed heap unchanged;
  - run-time speed unchanged within noise;
  - symbols +~180 in an Intent session;
  - Intent session load +3%.
- **The limitation, stated plainly:**
  - A vocabulary is a scope the loader resolves, not a run-time value.
  - Its members are stored as global bindings under their full names
    (`s/length`), as C++ namespaces are stored under mangled names.
  - First-class vocabulary contexts with run-time path lookup need new emitted
    runtime code, about 150–300 cells. Intent sessions have 58 free, so they
    wait for the code-space work (§6).

## 1. Audit

### What binding was

- **A set-word followed the run-time chain.** At run time a set-word walks
  the current context chain (`r_set` → `r_find_addr`): the function's
  activation, the activations it captured, then the global context.
  - It updates the first binding of that name it finds; if there is none, it
    adds a local to the current activation.
  - So a function's would-be local became a global write whenever a global of
    that name existed: when the function ran, not when it was written.
  - Probes `d2`/`d3` (GLON-LANG-1 §2.5) and the live Intent collisions (`k`,
    `face`) showed it.
- **Only parameters were lexical.** The loader resolves parameters to slot
  references (`T_BOUND`, FIB-OPT-P4). Set-word locals were deliberately
  untracked, because the runtime gives them slots in execution order.
- **A function context holds 16 bindings** (`R0S1_CTX_CAP`); an activation
  context is 64 cells on the return stack.
- **Natives are fixed:** S1's host operations are a closed set in frozen
  `s1.c`. Emitted S1 code cannot call C. Any new run-time behaviour is
  emitted code, which takes code cells.
- **The code budget:** the runtime is 12,884 cells. The libraries' `masm`
  brings an Intent session to 16,070 of 16,128, leaving 58 free.

| Source of `masm` | Code cells |
|---|---|
| `view_cmd.glon` | 992 |
| `strings-ext.glon` | 504 |
| the prelude | 416 |
| `strings.glon` | 304 |

### What the names were

- 918 global names in an Intent session's libraries, 894 of them
  "qualified" (`intent/`, `vid/`, `view/`, `lay/`, `s/`, `list/` ...).
- Every one was a single flat symbol: `s/length` was one word, not a path.
- Nothing stopped two libraries defining the same name. The last loaded won
  (`s/span` in GLON-PRATT against `strings-ext.glon`).
- Inside a library every reference spelled its own prefix.

### What depended on the old rule

A census of every `.glon` program in the repository
(`../diagnostics/vocab1-tools/census.py`):

| Pattern | Count |
|---|---|
| In-function writes to a plain global defined at the top level of the *same* file (games, simulators, Linda / tuple-space, GUI demos) | 305 writes in 28 programs |
| The same across files (the kaka game writes globals its main file defines) | a handful |
| Functions creating a global at run time that another function reads | 13 reads in 3 files |
| Libraries writing another library's plain global from a function | none |

So a blanket "set-words are always local" rule would have broken the
existing programs, while the hazard comes from names a reader cannot see.

## 2. The binding rules

Decided by the loader from the source, not by what exists at run time.

**1. A set-word in a function** (`x: ...`, its name plain) assigns:
- a parameter or variable of this function or an enclosing one, the
  innermost first, so closures update their enclosing variables as before;
- otherwise, **outside vocabularies only**, a global the reader can see from
  the source:
  - one defined at the top level of the same source;
  - or a data global (not a function, native or `masm` word) that an
    earlier source has already defined when this one is loaded.
- otherwise a new local of this function.

  A local's symbol is decorated: its spelling plus a trailing space, which no
  source can spell. No global can have it, so the runtime's unchanged set
  always makes or updates the local. Its slot and its cost are as before.

**2. Inside a vocabulary** a set-word in a function never reaches a member
or a global: it is always rule 1's first or last case.

**3. Explicit outer access:**

| Form | Means |
|---|---|
| plain name | an enclosing function's variable (lexical) |
| `vocab/member: value` | assign that member (it must exist) |
| `set 'name value` | assign the global `name`, making it if absent. The loader reads it as that global's own set-word and makes the binding at load; a parameter of an enclosing function with that name is refused. Free |
| `set word value` | a computed name: `binding.glon`'s `masm` (67 cells). Loaded by the native runner, not by the Intent host |
| `global/name` | read the global past a member of that name |
| `'local/name` | a local's own word, for inspection (the D1 debugger tests use it) |

**4. Reading a plain name** in a function: a parameter (a slot, as before),
then a local, then a member of the enclosing vocabularies (innermost first),
then the global. A member named like a built-in native (`s/+`, `s/=`,
`s/print`) does not hide the native: inside `s`, `+` is still integer
addition.

**5. What stays as it was:**
- top-level code (outside functions);
- `do`, `either` and `judge` blocks (they belong to the function that holds
  them);
- dialect blocks (`layout`, `panel`, `form`, `tabs`: their set-words name
  faces and are left alone);
- lit-words (data);
- function specs;
- computed specs and bodies (`func sp body`);
- `case` (its blocks become closures bound where they were written).

Two consequences:
- A function that reads a name before setting it, intending a global it then
  shadows, now reads its own unset local and fail-stops, loudly, where it
  used to silently share the global.
- Blocks `do`ne inside another library function to borrow its local names
  (REBOL-style dynamic binding) no longer see them. None of the code in the
  repository depends on that: the census and every suite pass.

## 3. Vocabularies

    vocabulary geo [
        origin: 0
        vocabulary point [
            make: func [x y] [ reduce [ x y ] ]
            x: func [p] [ block-at p 0 ]
            sum: func [p] [ + x p y p ]        ;; x and y: point's members
            y: func [p] [ block-at p 1 ]
        ]
        double-sum: func [p] [ * 2 point/sum p ]   ;; point resolved from geo
    ]
    geo/point/sum geo/point/make 3 4               ;; 7

- **Members** are the set-words at the body's top level, including inside
  top-level `do [ ]` blocks, and the names of nested vocabularies. They can
  be defined in any order, since the body is scanned first.
- **Storage:** each member is stored under its full name (`geo/point/sum`).
  Code outside the vocabulary calls it by that name.
- **Lexical resolution inside:** a plain name that is a member means the
  member, unless a parameter or local shadows it. A qualified name's head is
  resolved lexically: `point/sum` inside `geo` is `geo/point/sum`.
- **Ownership** (all load errors, naming the member):

| Refused | Example |
|---|---|
| a member the vocabulary does not have | `s/nope` |
| a definition (`func`, `masm`, `does`, `lambda`) of a member from outside its body | `s/length: func ...` in another file |
| a member defined twice | |
| `vocabulary` anywhere but a top level | |

  Assigning an existing member from outside is allowed: it is explicit, and
  that is how applications and tests set library state, such as
  `intent/heap-free: 1000`.

- **Reloading and reopening:**
  - A vocabulary may be reopened (`vocabulary s [ ... ]` in another file)
    to add members; three files do this for `s/`.
  - The same source loaded again in one session (a reload) may define its
    members again, because each member remembers its source.
- **Compatibility:**
  - Flat globals already defined under a vocabulary's prefix are adopted as
    its members when it is first declared (Intent's 384 `intent/` names).
  - A source loaded before the declaration is not checked, and its
    qualified names are the same symbols, so load order does not change
    what runs.
  - Code that uses `s/length` is unchanged.

## 4. Migrations

**`s/`** (`demo/shop/strings.glon`, `strings-ext.glon`, `strings-text.glon`;
the `glon-lib/` copies follow).
- Each file is a `vocabulary s [ ... ]` body. Members are defined, and used
  inside, by their plain names: 156 definitions and references rewritten by
  `migrate_s.py`.

  ```rebol
  length: func [a] [
      either raw-string? a [ raw-length a ] [ raise create-sin 'type 's/length a ]
  ]
  ```
- `raw-string?`, defined by the prelude before `s` exists, is an adopted
  member.
- SIN ids stay `'s/length` (lit-words are data).
- `view/glon/int_text_cache.glon` used to redefine `s/from-int` with a
  cached version. Ownership refused that, so it now defines `it/from-int` and
  replaces the member by assignment (`s/from-int: :it/from-int`): the
  replacement is visible in the source.

**`n/`** (`demo/shop/numbers.glon`, new): `min max abs sign clamp between?
digits sum`.
- Written natively: `clamp` is `max lo min a hi`.
- Application globals `min`, `max` and `abs` change nothing in it (test
  `numbers`).

**Intent** (`view/glon/intent_windows.glon`, the GLON-INST-3B window words).
- The file is `vocabulary intent [ ... ]`, which adopts `intent.glon`'s
  `intent/` names. Its functions use them plainly: `current`, `activate`,
  `engine-save`, `tag`.
- The window-record helpers are a nested vocabulary,
  `intent/window/record`, `instance`, `parent`, `create`, `destroy`,
  `forget`, called as `window/destroy` inside `intent` and as
  `intent/window/instance` from `intent.glon`.
- Assignments to Intent state are explicit (`intent/current: o`).
- All GLON-INST-3B window tests pass on it.

**What was not migrated:**
- the rest of `intent.glon` (2,800 lines), and `vid/`, `view/`, `lay/`,
  `list/`;
- they keep working as flat names, already protected by the binding rules;
- each can become a vocabulary file by file as above, with
  `../diagnostics/vocab1-tools/migrate_v.py`.

## 5. Measurements

Aligned builds (`-O2 -g -falign-functions=64 -falign-loops=32`) of HEAD
(GLON-INST-3B) against this tree, clean copies, alternating runs
(`vocab1-measure.log`).

| | Before | After |
|---|---|---|
| Code cells free, Intent sessions (membership, windows demo, Intent tests) | 58 | 58 |
| Emitted runtime | 12,884 cells | 12,884 cells |
| `binding.glon` (native runner only) | — | 67 cells |
| Globals, membership session | 1,063 | 1,064 |
| Function sites, membership session | 887 | 887 |
| Symbols, Intent libraries | 1,458 | 1,679 (180 decorated locals) |
| Symbols, + window library | 1,504 | 2,125 (adopting 400 `intent/` members interns their short names) |
| Symbols, membership session | 1,600 | 1,824 |
| Loader heap, membership session | 2,251,241 | 2,251,289 (+48 cells) |
| Managed heap | — | unchanged (no run-time objects added) |
| Membership session load and first build | 1,263–1,282 ms | 1,304–1,311 ms (+~3%: the pre-scans and adoption) |
| Native: 90,000 calls of a 3-local function | 4,629 / 4,686 / 4,771 ms | 4,630 / 4,689 / 4,773 ms |
| Intent pointer / key / switch, 1–100 instances (`inst2-scale`, 2 runs) | 1.34–1.42 / 2.42–2.55 / 7.8–8.3 ms | 1.33–1.44 / 2.45–2.62 / 7.9–8.5 ms |

- **Lookup cost is unchanged** because lookup is unchanged:
  - a decorated local is found where the plain one was;
  - a member is a global binding found by the same hash probe;
  - a qualified name is one symbol.
- **Symbol capacity:** 8,192 in 1M and 32,768 in 4M, against at most 2,125
  used. In the 64K native profile (512) the native runner's sessions use 65.

## 6. The limitation: vocabularies are not run-time values

**What a run-time vocabulary would need.** A vocabulary as a context value
in memory, with `s/length` resolved through it when it runs, needs emitted
runtime code that does not exist:
- a context allocation of a chosen capacity (activation contexts are fixed
  at 16 bindings and 64 cells);
- evaluating a body with that context current;
- path resolution in the word and set-word evaluators;
- member checks.

At the sizes of comparable emitted routines (lookup, set, append, mkctx),
that is about 150–300 cells.

**Why it can't go in now.** An Intent session has 58 cells free. The only
way to make room is the code-space work on another branch, which this
milestone must not duplicate.

**What it would cost at run time.** Each qualified call would add one
context lookup: ~0.9 µs measured for an extra global read (GLON-LANG-1
§2.9). GLON-VOCAB-V1's linked list took ~128 µs.

**What exists now instead:**
- vocabularies as loader scopes;
- ownership checked at load;
- members stored as globals under their full names.

What is not possible yet:
- passing a vocabulary as a value;
- adding members at run time;
- `vocab/member` paths computed at run time (`get 'intent/current` works,
  since it is one symbol).

When code space allows, the run-time form can replace the storage without
changing source: the scopes and names are already right.

## 7. Compatibility

- **All existing programs keep their meaning** except where a reader could
  not see the global they wrote:
  - kaka, patrol, the desktop app, the traffic simulator and the GUI demos
    pass unchanged (§8);
  - the kaka game's cross-file writes work through the earlier-source data
    rule.
- **The D1 debugger's tests** inspect set-word locals as `'local/x` (five
  places); parameters as before.
- **`demo/shop/primer.txt`** states the new set-word rule (§6 and the Rebol
  table). `GLON-PRIMER.md` and `primer.html` were regenerated, and all 30
  examples still hold, including the closure counter.
- **Not compatible: the frozen GLON-PRATT experiment.**
  - `make -C experiments/glon-pratt test` builds a fresh runner with the
    canonical `strings.glon`, and its `s-extra.glon` then defines new `s/`
    members (`s/raw-at` ...) from outside the vocabulary. That is refused.
  - Its sources are pinned, so it is left failing. The migration is to wrap
    `s-extra.glon`'s definitions in `vocabulary s [ ... ]` (or give them its
    own vocabulary).
  - This is the collision GLON-PRATT-V004 warns about (`s/span`), now
    caught at load.
- **Other copies of the string library** are unaffected: in `demos/kaka`,
  `experiments/glon-*-web` and `modules/`, no session declares the `s`
  vocabulary.
- **`set` and `vocabulary` are now loader words:**
  - `set 'name` with a literal lit-word is read by the loader;
  - a local or parameter named `set` (`s/scan-set: func [s i set]`) is still
    an ordinary variable.

## 8. Tests and files

**`tests/vocab1/run.sh`** (`make test-vocab1`, run by `make test`): 17
checks through the native runner, each file its own source.

| Check | What it shows |
|---|---|
| nested | three-level qualification; lexical members; relative `point/sum` |
| numbers | `n/`, with application globals `min`/`max`/`abs` |
| reuse | `size`/`area` in two vocabularies and a global `size` |
| library-locals | a library's `k`, `face`, `i` against application globals loaded after it. **Fails on the old runtime:** `#[func]` instead of `5 6 42 7 6 1000` |
| outer-mutation | a closure counter; `set 'total` from a function; `tally/count:` from a member function; a local shadowing a member |
| computed-set | `set w value` (`binding.glon`); `set 'tally/hits`, `set 'last-hit` inside a vocabulary |
| computed-spec | `func sp b` with `sp` a local |
| load-order | `ord/x` used by a source loaded before `ord` exists |
| later-global | a global defined in a later source is not reached. **Old runtime:** `1 1 1 1` |
| reload | the same source twice in one session |
| refusals | unknown member, outside definition, outside redefinition, duplicate, `global/x:`, a vocabulary inside a function, `set` past an enclosing parameter |

**Full regression** (`vocab1-regression-summary.log`), on the final code:
all 28 suites exit 0.
- root `make test`, which includes `test-vocab1`, the D1 debugger and the
  primer doc-tests;
- `check-frozen-s1.sh`;
- `test-inst1`/`2`/`3`, `test-win3`, `test-intent`, `test-gui-p2`;
- `test-members` (4M and 1M), `test-intent7`, `mem1-app`;
- view `test`, `test-gui-f1b`, `test-wg-runner`;
- the site, 64K VID and global capacity profiles;
- `test-lib1`, the vocabulary V-1 script and the six GUI shell scripts;
- `kaka-test` (with its render, damage, terminal, wave, fighter and life
  suites), `patrol-test`, `desktop-test`.

Two runs before it found the bugs fixed in this milestone:
- a computed `func` spec held in a local was left unrenamed (`intent/
  bind-one`'s `func sp a`), which broke every Intent session built on the
  INTENT test layouts;
- a library reloaded in the same session was refused as a redefinition.

**Not run:**
- the X11-only suites, which need a display;
- the WASM tests, which need emscripten and the CI-built `glon.wasm`. The
  WASM builds embed the same loader and will need rebuilding.
- `make -C experiments/glon-pratt test` fails as described in §7.

**Files:**
- `r0_s1_runtime.c`: the loader. The pre-scanner, scopes, rewriting, the
  vocabulary registry, `set`/`local/`/`global/`, and the `set` entry for
  `masm`.
- `r0_s1_show.c`: decorated locals print as their names.
- `glon.c` and `Makefile`: `binding.glon`, optional; `test-vocab1`.
- `demo/shop/binding.glon`, `numbers.glon` (new); the three string files;
  `view/glon/int_text_cache.glon`, `intent.glon`, `intent_windows.glon`;
  `r0_s1_debug_tests.c`.
- `../diagnostics/vocab1-tools/`: the census, the two conversion scripts,
  the symbol counter and the measurement script.
