# B4 task fixtures

Generated deterministically by `tools/gen_fixtures.py`.

For each task `<name>`:

- `<name>.glon` — A, textual Glon.
- `<name>.b4b` — B, semantically tagged 32-bit words.
- `<name>.b4c` — C, direct machine-oriented 32-bit words.
- `<name>.md` — independent task specification and expected result.

| Task | What it exercises | Expected |
|---|---|---:|
| `fib` | recursion, comparison, arithmetic, conditional | 2584 |
| `sum` | two-parameter recursion, accumulator | 125250 |
| `max3` | multiple arguments, nested conditionals | 7 |
| `fact` | recursion and tagged multiplication | 3628800 |
| `diff` | absolute difference, comparisons | 25 |
| `nested` | nested calls and two functions | 7 |
| `forward` | forward reference to a later function | 42 |

All fixtures use the same B3 target: convention A and the six native
operations CALL, RET, >R, R>, R@, BRANCH.
