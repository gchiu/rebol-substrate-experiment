# B6 matched A/D analysis

| Metric | A | D |
|---|---:|---:|
| trials | 24 | 24 |
| first_success | 6 | 5 |
| final_success | 23 | 24 |
| attempts | 50 | 51 |
| repairs | 26 | 27 |
| cost | 0.10435755 | 0.07708395 |
| model_step_seconds | 1015.904 | 748.826 |
| process_seconds | 1244.964323 | 984.924547 |
| verification_seconds | 4.62412 | 4.516772 |
| validation_seconds | 0 | 0.007783 |
| cost_per_attempt | 0.004348231 | 0.003211831 |
| cost_per_success | 0.004537285 | 0.003211831 |
| tokens_per_success | 8145.22 | 5953.71 |

| Tokens | A | D |
|---|---:|---:|
| input | 17881 | 19221 |
| output | 2509 | 2487 |
| reasoning | 166950 | 121181 |
| total | 187340 | 142889 |

## First-attempt diagnostics

**A**: {'compiler_or_semantics': 18, 'pass': 6}
**D**: {'missing_top_level': 15, 'pass': 5, 'invalid_function_reference': 1, 'invalid_control_flow': 2, 'expected_expression': 1}
