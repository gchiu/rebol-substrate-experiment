# B4 generation-and-repair harness

**Status: not run.** No LLM was available through this development environment,
so no model-token, time-to-correct or productivity claim is made. This directory
provides the fixtures and harness needed for a controlled trial.

## Tasks

The task specifications are the Markdown files in `../tasks/`:

`fib`, `sum`, `max3`, `fact`, `diff`, `nested`, `forward`.

Each has an independent expected result in `../b4_main.c` and a task
description in `tasks/<name>.md`.

## Representations

- `A` textual Glon.
- `B` semantically tagged 32-bit words.
- `C` direct machine-oriented 32-bit words.

`prompts/A.md`, `prompts/B.md`, `prompts/C.md` contain the equivalent format
instructions. A trial prompt is the task specification concatenated with the
representation format.

## Trial protocol

For each task and representation:

1. Start a fresh model context with the representation prompt and task.
2. Allow at most one initial artifact and three repair attempts.
3. Each attempt is compiled by the measured B4 front-end and run against the
   expected result; compile errors are shown back to the model for repair.
4. Record: model/version/configuration, temperature and token budget, prompt,
   initial artifact, compile/test failures, each repair artifact, tokens
   consumed if the API exposes them, wall-clock time to final correct artifact,
   number of attempts, and final pass/fail.
5. Use the same budget and repair count for all representations.

The harness does not itself call a model. If an `LLM_CMD` command is available:

```
LLM_CMD='your-model-cli --stdin' generation/run_trial.sh fib A /tmp/fib.glon
```

`run_trial.sh` concatenates the task and format prompts, sends them to
`LLM_CMD`, saves each artifact as `OUTPUT.attemptN`, and runs
`../build/b4 --verify`. On failure it appends the compiler/test output and the
failed artifact to the prompt, for up to three repair attempts. It uses the
same budget and repair count for all representations.

## What is not controlled

- The model's prior familiarity with Glon, tokens or assembly.
- The tokenizer; bytes and lexical tokens are not model tokens.
- Artifact authoring time; the B/C fixtures in `../tasks/` were prepared by
  `tools/gen_fixtures.py` outside the measured compilers.

These must be recorded before any productivity comparison is claimed.
