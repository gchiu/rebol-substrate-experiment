
## Output format: B (semantically tagged words)

Write a whitespace-separated list of 32-bit hexadecimal words, one token per
word. The harness converts it to the little-endian binary artifact expected by
the B4-B decoder. Do not write prose or Markdown fences; return only hex words.

The first word is `42344231`. Every later word is

```
(role << 24) | operand
```

Roles:

1 FUNC (operand `id<<8 | arity`), 2 SEQ_BEGIN, 3 SEQ_END, 4 TOP_BEGIN,
5 TOP_END, 6 INT (signed 24-bit), 7 PARAM, 8 CALL (`id<<8 | arity`),
9 BIN (0 +, 1 -, 2 *, 3 <, 4 <=, 5 =, 6 >, 7 >=), 10 EITHER,
11 THEN_BEGIN, 12 THEN_END, 13 ELSE_BEGIN, 14 ELSE_END.

Functions are definitions followed by a sequence; the top-level expression follows
after all functions in TOP. Negative integer constants may use a signed 24-bit
two's-complement operand or subtraction from zero.

Identifier rules required by the B4-B decoder:

- Function IDs are positive integers starting at 1. Function ID 0 is invalid.
- Function IDs must be unique.
- Function arity is 0 through 8. A CALL's arity must equal the target
  function's arity.
- Parameter indices are zero-based and must be less than the current
  function's arity.
- All function definitions and their SEQ blocks precede the single TOP block.
  TOP_END terminates the top-level expression; no tokens may follow it.
