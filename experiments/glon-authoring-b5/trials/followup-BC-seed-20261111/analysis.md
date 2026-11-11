# B5 OpenCode trial analysis

Model: `deepseek/deepseek-flash`; seed: 20261111; tasks: 12; trials: 24.

| Rep | First success | Final success | Evaluable | Timeout-affected | Attempts | Repairs | Tokens | Cost | Model s | Process s | Cost/attempt | Cost/success |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 0/0 | 0/0 | 0/0 | 0 | 0 | 0 | 0 | $0.000000 | 0.00 | 0.00 | n/a | no success |
| B | 11/12 | 12/12 | 12/12 | 0 | 14 | 2 | 161245 | $0.092908 | 940.24 | 994.47 | $0.007742 | $0.007742 |
| C | 0/12 | 0/12 | 12/12 | 0 | 48 | 36 | 1036806 | $0.606714 | 6222.26 | 6366.90 | $0.050560 | no success |

## Per task

| Task | Rep | First | Final | Timeouts | Attempts | Tokens | Cost |
|---|---|---:|---:|---:|---:|---:|---:|
| ack | B | True | True | 0 | 1 | 1862 | $0.000863 |
| choose | B | True | True | 0 | 1 | 9641 | $0.005530 |
| clamp | B | True | True | 0 | 1 | 7539 | $0.004265 |
| is_even | B | True | True | 0 | 1 | 8141 | $0.004627 |
| max_neg | B | False | True | 0 | 3 | 47641 | $0.027549 |
| median3 | B | True | True | 0 | 1 | 10742 | $0.006192 |
| min3 | B | True | True | 0 | 1 | 9147 | $0.005235 |
| pow2 | B | True | True | 0 | 1 | 21176 | $0.012453 |
| quad | B | True | True | 0 | 1 | 10974 | $0.006331 |
| range_sum | B | True | True | 0 | 1 | 9206 | $0.005268 |
| sign_neg | B | True | True | 0 | 1 | 12983 | $0.007534 |
| tri | B | True | True | 0 | 1 | 12193 | $0.007061 |
| ack | C | False | False | 0 | 4 | 74356 | $0.043013 |
| choose | C | False | False | 0 | 4 | 88736 | $0.051667 |
| clamp | C | False | False | 0 | 4 | 72046 | $0.042135 |
| is_even | C | False | False | 0 | 4 | 90883 | $0.053361 |
| max_neg | C | False | False | 0 | 4 | 70511 | $0.041237 |
| median3 | C | False | False | 0 | 4 | 86657 | $0.050876 |
| min3 | C | False | False | 0 | 4 | 104949 | $0.061285 |
| pow2 | C | False | False | 0 | 4 | 82455 | $0.048220 |
| quad | C | False | False | 0 | 4 | 84424 | $0.049555 |
| range_sum | C | False | False | 0 | 4 | 89727 | $0.052482 |
| sign_neg | C | False | False | 0 | 4 | 114159 | $0.067396 |
| tri | C | False | False | 0 | 4 | 77903 | $0.045487 |

## Failure categories

**A** first: {}; final: {}; all attempts: {}
**B** first: {'incorrect program semantics': 1}; final: {}; all attempts: {'incorrect program semantics': 2}
**C** first: {'invalid representation/encoding': 11, 'incorrect program semantics': 1}; final: {'incorrect program semantics': 5, 'invalid representation/encoding': 7}; all attempts: {'invalid representation/encoding': 33, 'incorrect program semantics': 15}

## Infrastructure caveat
