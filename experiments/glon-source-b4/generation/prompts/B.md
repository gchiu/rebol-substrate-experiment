
## Output format: B (semantically tagged words)

Write a little-endian binary file of 32-bit words. The first word is
`0x42344231`. Each later word is `(role << 24) | operand`:

1 FUNC (operand id<<8|arity), 2 SEQ_BEGIN, 3 SEQ_END, 4 TOP_BEGIN,
5 TOP_END, 6 INT (signed 24-bit), 7 PARAM, 8 CALL (id<<8|arity),
9 BIN (0 +, 1 -, 2 *, 3 <, 4 <=, 5 =, 6 >, 7 >=), 10 EITHER,
11 THEN_BEGIN, 12 THEN_END, 13 ELSE_BEGIN, 14 ELSE_END.

Functions are definitions followed by a SEQ; the top-level expression follows
after all functions in TOP. Return only the binary artifact.
