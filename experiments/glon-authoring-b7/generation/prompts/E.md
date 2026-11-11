
## Output format: A (textual Glon)

Write one textual Glon source using only this subset:

- `name: func [params] [ body ]` top-level definitions;
- prefix calls `f a b`, integer literals, parameters;
- `+ - *`, `< <= = > >=`, and `either cond [then] [else]`;
- no strings, blocks as values, locals, closures, `if`, `/`, or other natives.

Return only the source text. Comments start with `;`.

The benchmark submission must include the specified top-level invocation after the function definitions. Definitions alone are a valid Glon module, but this benchmark executes the final top-level expression to obtain its result; a definitions-only artifact does not produce a test result.
