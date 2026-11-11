--- experiments/glon-authoring-b7/generation/prompts/A.md	2026-11-11 18:06:57.366650000 +1300
+++ experiments/glon-authoring-b7/generation/prompts/E.md	2026-11-11 18:06:57.706332800 +1300
@@ -9,3 +9,5 @@
 - no strings, blocks as values, locals, closures, `if`, `/`, or other natives.

 Return only the source text. Comments start with `;`.
+
+The benchmark submission must include the specified top-level invocation after the function definitions. Definitions alone are a valid Glon module, but this benchmark executes the final top-level expression to obtain its result; a definitions-only artifact does not produce a test result.
