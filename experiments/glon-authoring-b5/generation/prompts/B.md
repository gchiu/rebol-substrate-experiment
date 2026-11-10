
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
