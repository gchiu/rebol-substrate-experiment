
## Output format: C (direct machine words)

Write a little-endian binary file of 32-bit words. The first word is
`0x43344231`. Each later word is `(op << 24) | operand`:

1 ENTRY (label id), 2 LABEL, 3 LIT (raw tagged cell), 4 DUP, 5 DROP,
6 FETCH, 7 STORE, 8 ZBRANCH (label), 9 HOST (raw host id), 10 CALL (label;
native), 11 RET, 12 TO_R, 13 FROM_R, 14 R_FETCH, 15 BRANCH (label; native),
16 HALT.

There is no function/arity metadata; encode the B3 convention-A prologue
(`TO_R` per argument), explicit parameter reads (R_FETCH for parameter 0;
otherwise LIT 2 FETCH LIT i HOST 0 FETCH), epilogue (`FROM_R DROP` per
argument, then RET), and an ENTRY label for the top-level block. Return only
the binary artifact.
