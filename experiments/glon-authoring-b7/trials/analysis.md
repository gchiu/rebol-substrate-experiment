# B7 matched A/E analysis

| Metric | A | E |
|---|---:|---:|
| trials | 24 | 24 |
| first_success | 10 | 17 |
| final_success | 24 | 24 |
| attempts | 41 | 36 |
| repairs | 17 | 12 |
| cost | 0.0421296 | 0.06543855 |
| model_step_seconds | 416.484 | 649.83 |
| process_seconds | 597.196342 | 792.882589 |
| verification_seconds | 3.019798 | 2.79607 |
| validation_seconds | 0.00498 | 0.004858 |
| cost_per_attempt | 0.0017554 | 0.002726606 |
| cost_per_success | 0.0017554 | 0.002726606 |
| tokens_per_success | 3383.04 | 5010.38 |

| Tokens | A | E |
|---|---:|---:|
| input | 14636 | 14913 |
| output | 1948 | 1908 |
| reasoning | 64609 | 103428 |
| total | 81193 | 120249 |

## First-attempt diagnostics

**A**: {'missing_top_level': 8, 'invalid_control_flow': 4, 'pass': 10, 'expected_expression': 1, 'invalid_function_reference': 1}
**E**: {'pass': 17, 'invalid_control_flow': 6, 'invalid_function_reference': 1}
