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
