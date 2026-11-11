
## Output format: A (textual Glon)

Write one textual Glon source using only this subset:

- `name: func [params] [ body ]` top-level definitions;
- prefix calls `f a b`, integer literals, parameters;
- `+ - *`, `< <= = > >=`, and `either cond [then] [else]`;
- no strings, blocks as values, locals, closures, `if`, `/`, or other natives.

Return only the source text. Comments start with `;`.
