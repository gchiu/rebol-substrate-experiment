# B5 task suite

Generated deterministically by `tools/gen_tasks.py`. Each task has a
specification (`<name>.md`) and equivalent A/B/C artifacts:

- `<name>.glon` — textual Glon.
- `<name>.b4b` — semantically tagged words (B4 B format).
- `<name>.b4c` — direct machine words (B4 C format).

| Task | Exercises | Expected |
|---|---|---:|
| `tri` | recursion, arithmetic, edge at n<=0 | 55 |
| `pow2` | recursion, multiplication | 1024 |
| `min3` | multiple arguments, nested branching | 4 |
| `clamp` | multiple arguments, nested branching, high-bound edge | 10 |
| `sign_neg` | negative input, branching | -1 |
| `range_sum` | two-argument recursion, accumulator | 42 |
| `choose` | two-way recursion, binomial base cases | 15 |
| `is_even` | recursion by subtraction, parity edge | 1 |
| `ack` | deep recursion, multiple arguments | 9 |
| `quad` | nested calls and forward reference | 20 |
| `median3` | multiple arguments, nested comparisons | 7 |
| `max_neg` | three negative inputs, comparisons | -4 |

Reference implementations and expected values are in `b5_main.c`, independent
of this fixture generator. Negative constants are represented as subtraction
from zero in the fixtures so all three representations lower to the same code.
