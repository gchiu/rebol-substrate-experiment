# B5 follow-up prompt corrections

The original prompts are preserved at `../A.md`, `../B.md`, `../C.md`.
The follow-up uses:

- `A.md` — no follow-up run; A's original completed run is the reference.
- `B.md` — original B prompt plus the identifier rules below.
- `C.md` — identical to the original C prompt (timeout only was corrected).

## B identifier corrections

Checked against the actual B4-B decoder (`b4_tagged.c`):

- `FUNC` and `CALL` decode the function id from `operand >> 8`; the decoder
  rejects id 0, so ids must be positive. The original prompt did not state
  this.
- Function ids must be unique; duplicate definitions are rejected.
- Arity is decoded from `operand & 0xff`; the decoder rejects arity above 8.
- A CALL must use the target function's arity; the decoder rejects a mismatch
  after code emission.
- `PARAM` uses a zero-based index and must be less than the current function's
  arity.
- Function definitions and their sequences must precede the single TOP block;
  the decoder rejects tokens after TOP_END.

No binary encoding, token role, compiler, decoder or execution semantics were
changed. No example solution was added.
