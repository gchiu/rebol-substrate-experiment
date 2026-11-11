# B5 OpenCode trial analysis

Model: `deepseek/deepseek-flash`; seed: 20261110; tasks: 12; trials: 36.

| Rep | First success | Final success | Evaluable | Timeout-affected | Attempts | Repairs | Tokens | Cost | Model s | Process s | Cost/attempt | Cost/success |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 3/12 | 12/12 | 12/12 | 0 | 24 | 12 | 76685 | $0.042250 | 381.58 | 523.75 | $0.003521 | $0.003521 |
| B | 0/12 | 12/12 | 11/12 | 1 | 27 | 15 | 234217 | $0.133951 | 1243.01 | 1601.24 | $0.011163 | $0.011163 |
| C | 0/12 | 0/12 | 1/12 | 11 | 48 | 36 | 508700 | $0.296129 | 2867.38 | 6332.99 | $0.024677 | no success |

## Per task

| Task | Rep | First | Final | Timeouts | Attempts | Tokens | Cost |
|---|---|---:|---:|---:|---:|---:|---:|
| ack | A | False | True | 0 | 2 | 3041 | $0.001514 |
| choose | A | True | True | 0 | 1 | 4579 | $0.002612 |
| clamp | A | False | True | 0 | 2 | 2168 | $0.000991 |
| is_even | A | True | True | 0 | 1 | 6124 | $0.003536 |
| max_neg | A | False | True | 0 | 3 | 3011 | $0.001277 |
| median3 | A | False | True | 0 | 2 | 2319 | $0.001081 |
| min3 | A | False | True | 0 | 2 | 2396 | $0.001131 |
| pow2 | A | False | True | 0 | 2 | 6610 | $0.003670 |
| quad | A | False | True | 0 | 4 | 38430 | $0.022372 |
| range_sum | A | False | True | 0 | 2 | 3716 | $0.001929 |
| sign_neg | A | False | True | 0 | 2 | 2077 | $0.000944 |
| tri | A | True | True | 0 | 1 | 2214 | $0.001192 |
| ack | B | False | True | 1 | 4 | 45434 | $0.026355 |
| choose | B | False | True | 0 | 2 | 16577 | $0.009450 |
| clamp | B | False | True | 0 | 2 | 20768 | $0.011974 |
| is_even | B | False | True | 0 | 2 | 22519 | $0.013041 |
| max_neg | B | False | True | 0 | 3 | 37803 | $0.021787 |
| median3 | B | False | True | 0 | 2 | 12460 | $0.006946 |
| min3 | B | False | True | 0 | 2 | 16152 | $0.009198 |
| pow2 | B | False | True | 0 | 2 | 16402 | $0.009379 |
| quad | B | False | True | 0 | 2 | 11048 | $0.006180 |
| range_sum | B | False | True | 0 | 2 | 10604 | $0.005891 |
| sign_neg | B | False | True | 0 | 2 | 13615 | $0.007715 |
| tri | B | False | True | 0 | 2 | 10835 | $0.006035 |
| ack | C | False | False | 1 | 4 | 43763 | $0.025397 |
| choose | C | False | False | 1 | 4 | 67926 | $0.039706 |
| clamp | C | False | False | 1 | 4 | 27930 | $0.015989 |
| is_even | C | False | False | 3 | 4 | 14667 | $0.008550 |
| max_neg | C | False | False | 1 | 4 | 44232 | $0.025741 |
| median3 | C | False | False | 2 | 4 | 35305 | $0.020670 |
| min3 | C | False | False | 1 | 4 | 46085 | $0.026294 |
| pow2 | C | False | False | 3 | 4 | 24406 | $0.014409 |
| quad | C | False | False | 0 | 4 | 92269 | $0.054126 |
| range_sum | C | False | False | 2 | 4 | 44185 | $0.025831 |
| sign_neg | C | False | False | 1 | 4 | 31468 | $0.018091 |
| tri | C | False | False | 2 | 4 | 36464 | $0.021325 |

## Failure categories

**A** first: {'invalid representation/encoding': 9}; final: {}; all attempts: {'invalid representation/encoding': 12}
**B** first: {'invalid representation/encoding': 12}; final: {}; all attempts: {'invalid representation/encoding': 13, 'incorrect program semantics': 1, 'calling-convention/stack': 1}
**C** first: {'invalid representation/encoding': 11, 'incorrect program semantics': 1}; final: {'incorrect program semantics': 5, 'invalid representation/encoding': 7}; all attempts: {'invalid representation/encoding': 38, 'incorrect program semantics': 10}

## Infrastructure caveat

B: 1/12 trials had at least one 170-180 s zero-token adapter timeout. These are adapter/infrastructure failures, not valid model-generation outcomes.
C: 11/12 trials had at least one 170-180 s zero-token adapter timeout. These are adapter/infrastructure failures, not valid model-generation outcomes.
