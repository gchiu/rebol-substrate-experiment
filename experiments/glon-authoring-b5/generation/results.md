# B5 controlled trial results

Model: `deepseek/deepseek-flash`; seed `20261110`; 12 tasks × A/B/C.

- A: first 3/12, final 12/12; 24 attempts; 76,685 tokens; $0.042250.
- B: first 0/12, final 12/12; 27 attempts; 234,217 tokens; $0.133951.
- C: first 0/12, final 0/12 under the 180 s adapter cap; 48 attempts;
  508,700 tokens; $0.296129 spent. **C is not evaluable**: 11/12 trials had
  zero-token adapter timeouts. The adapter now defaults to 900 s.

C was interrupted by an adapter timeout defect, not a model-generation result.
One B `ack` trial also had one timeout-affected call but succeeded. Raw logs,
artifacts, sessions, tokens and costs are in `../trials/`.


## Follow-up

B corrected (seed 20261111, 900 s): first 11/12, final 12/12, 14 attempts,
161,245 tokens, $0.092908. Original B: first 0/12, final 12/12, 27 attempts,
234,217 tokens, $0.133951. The documentation correction reduced tokens and
cost by about 31% and repairs from 15 to 2.

C follow-up (900 s): 0/12 final, 48 attempts, 1,036,806 tokens, $0.606714;
0 timeouts, 12/12 evaluable. C smoke `tri` passed after 3 repairs but full
`tri` failed.

A original remains the reference: first 3/12, final 12/12, 76,685 tokens,
$0.042250. Among completed runs A used the least total effort; B corrected
improved first-attempt reliability but cost about 2.2× A.
