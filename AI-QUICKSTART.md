# Glon AI Quick Start

Glon Developer Preview 1 (`v0.1.0-alpha.2`) is an experimental execution
substrate intended primarily for AI-generated applications.

## Workflow

1. Download or clone the release.
2. Give the extracted directory to an AI coding agent.
3. Describe the desired application in ordinary language.
4. Let the agent inspect `AGENTS.md`, `VOCABULARY.json`, `modules/` and
   `examples/`.
5. Generate the Glon application.
6. Build and execute it.
7. Test it against the original human intention.

## Suggested first prompt

> Read AGENTS.md and the released Glon vocabulary documentation. Build a small
> HTTPS application in Glon that displays a web page and provides a JSON API.
> Use existing vocabularies. Do not invent syntax or modify the interpreter.
> Build and execute the application, test its endpoints and report the results.

## Suggested persistence prompt

> Read AGENTS.md and release/WINDOWS-QUICKSTART.md or the Linux quick start.
> Extend the HTTPS application with file-backed persistence. Use the existing
> file vocabulary. Test a restart and report what survived.

## Constrained deployment

The constrained profile needs no large-memory build:

```sh
make glon MODULES=http
./glon --http-cert tests/https-g1/.cert/server.pem examples/https-g1/server.glon
```

It has a 7,232-cell managed heap and a 14,800-cell loader. Use it for small
applications and constrained-device experiments. The large profile is selected
with `MEM_PROFILE=large`.

## Verification checklist for an agent

- The application loaded with its required libraries.
- The documented port is free before launch.
- `curl`/browser tests match the intended status, type and body.
- Invalid input does not mutate state.
- Persistence survives an orderly restart if claimed.
- The agent reports actual observed results and any failures.

Do not claim production readiness, long-term reliability or concurrent Glon
request handling. See `AGENTS.md` and the release documentation for limits.
