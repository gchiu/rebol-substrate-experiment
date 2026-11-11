
## Output format: C (direct machine words)

Write a whitespace-separated list of 32-bit hexadecimal words, one token per
word. The harness converts it to the little-endian binary artifact expected by
the B4-C assembler. Do not write prose or Markdown fences; return only hex
words.

The first word is `43344231`. Every later word is

```
(op << 24) | operand
```

Ops:

1 ENTRY (label id), 2 LABEL, 3 LIT (raw tagged cell), 4 DUP, 5 DROP,
6 FETCH, 7 STORE, 8 ZBRANCH (label), 9 HOST (raw host id), 10 CALL (label;
native), 11 RET, 12 TO_R, 13 FROM_R, 14 R_FETCH, 15 BRANCH (label; native),
16 HALT.

There is no function/arity metadata; encode the B3 convention-A prologue
(`TO_R` per argument), explicit parameter reads (R_FETCH for parameter 0;
otherwise LIT 2 FETCH LIT i HOST 0 FETCH), epilogue (`FROM_R DROP` per
argument, then RET), and an ENTRY label for the top-level block. For negative
constants, emit LIT 0, LIT n, HOST 1 (subtraction), because LIT operands are
unsigned 24-bit cells.
