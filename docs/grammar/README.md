# Glon grammar and railroad diagrams

This directory contains the generated, self-contained grammar reference for the
verified Glon reader.

## Files

| File | Role |
|---|---|
| `glon-grammar.ebnf` | **Canonical machine-readable grammar.** Edit this, not the generated output. |
| `generate.py` | Deterministic generator for SVG, HTML and manifest. Python standard library only. |
| `verify.py` | Checks grammar/document agreement, diagram coverage, deterministic regeneration, links, well-formed SVG, frozen S1 and implementation files. |
| `index.html` | Standalone HTML reference with inline SVG, navigation, zoom controls and implementation/unresolved notes. Opens directly with `file://`. |
| `railroad/<production>.svg` | One traditional railroad diagram per EBNF production. |
| `railroad/manifest.json` | Production list, grammar hash, SVG hashes and byte sizes. |

The canonical grammar is the verified EBNF from
`GLON-SYNTAX-DIAGRAMS.md`. `GLON-LANGUAGE-SPEC.md` repeats the same production
set. The generator does not change syntax.

## Regenerate

From the repository root:

```sh
python3 docs/grammar/generate.py
```

Or from this directory:

```sh
python3 generate.py
```

Generation is deterministic: no timestamps, randomness or network access.
The generator removes stale `railroad/*.svg` files, then writes the diagrams,
manifest and `index.html`.

## Verify

```sh
python3 docs/grammar/verify.py
```

The verifier checks:

- canonical grammar, `GLON-SYNTAX-DIAGRAMS.md` EBNF and
  `GLON-LANGUAGE-SPEC.md` EBNF agree;
- every production has exactly one SVG and one HTML section;
- all generated files match a fresh in-memory generation byte for byte;
- two generations are identical;
- all HTML anchors and relative links resolve, with no CDN/network reference;
- every SVG is well-formed XML and network-free;
- `./check-frozen-s1.sh` passes and implementation/frozen files are unchanged
  from `HEAD`.

It writes no files.

## Open the HTML reference

Windows: open `docs\grammar\index.html` directly in Edge, Chrome or Firefox
(for example, double-click it, or run
`start docs\grammar\index.html` from the repository root). No web server is
needed: CSS, JavaScript and all diagrams are inline. The only links that leave
the page are relative links to repository Markdown documents.

On Linux/WSL, `xdg-open docs/grammar/index.html` may work if a browser bridge
is configured; otherwise use the Windows path above.

## Structure of the reference

The page separates:

1. **Lexical grammar** — bytes, whitespace, tokens, word forms, strings,
   integers, private masm tokens.
2. **Structural grammar** — source envelope, block, item, form, `func`,
   `masm`, `masm-block`.
3. **Arity-driven grouping** — explicitly not EBNF; it is an evaluation rule.
4. **Implementation-specific parsing behaviour** and **unresolved cases**.

These sections deliberately do not claim that unresolved semantic behaviour is
verified. See `GLON-SPEC-VERIFICATION.md` §7 and `GLON-LANGUAGE-SPEC.md` §11.3.

## Future website integration

`index.html` is a static, self-contained page. To publish it without changing
the existing Glon site, copy or serve the whole `docs/grammar/` directory at a
stable path (for example `/grammar/`) and keep `railroad/` beside
`index.html`. The page has no build-time or runtime dependencies.

Relative links point to the sibling `../language/` directory, which is where the
published language reference and Markdown source copies live. Keep that layout
when publishing `grammar/`; update `DOC_LINKS` in `generate.py` and regenerate
if the path changes. The verifier catches broken links.

## Adding or changing a production

1. Edit `glon-grammar.ebnf` only after the language specification/verification
   pass authorises the change.
2. Update the matching EBNF blocks in `GLON-SYNTAX-DIAGRAMS.md` and
   `GLON-LANGUAGE-SPEC.md` (or explain why the canonical file is ahead).
3. Add any production-specific note in `PRODUCTION_NOTES` in `generate.py`.
4. Classify the production in `LEXICAL` or `STRUCTURAL`.
5. Run `generate.py`, then `verify.py`.
6. Commit the grammar, generator change, generated SVG/HTML and manifest
   together.

Do not edit generated `railroad/*.svg`, `railroad/manifest.json` or
`index.html` by hand. Do not add CDN links, remote fonts, remote scripts or
runtime fetches.
