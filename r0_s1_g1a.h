/* r0_s1_g1a.h - G1A: browser-hosted Glon/WASM application skeleton.
 *
 * This is the first real Glon *application* layer above the frozen R0/S1
 * substrate. It provides the two browser-agnostic primitives the G1A demo
 * needs, split exactly like M3C (generic mechanics here, policy in GLON):
 *
 *   - r0_s1_g1a_render_fragment : the `<% ... %>` template engine. Scans a
 *     bundled fragment for embedded Glon, evaluates each embedded block with
 *     the ordinary R0 evaluator (r0_s1_parse + r0_s1_run), stringifies each
 *     non-NONE result, and assembles the rendered HTML into a plain C buffer.
 *
 *   - r0_s1_g1a_route : the routing primitive. Binds `current-route` to the
 *     route token, runs the GLON `route` block (the *application's* routing
 *     decision + state mutation live there), then renders the fragment the
 *     router selected.
 *
 * These functions are deliberately browser-agnostic: they write rendered HTML
 * into a caller-provided buffer and know nothing about the DOM. The WASM host
 * layer (standalone/glon.c) supplies the actual browser imports (host_set_html)
 * and exports (glon_route) on top of this. This keeps the template engine and
 * router fully testable headlessly (see r0_s1_g1a_tests.c and
 * demo/shop/node_test.js).
 *
 * The frozen S1 substrate and the R0 evaluator/parser are untouched; this
 * layer adds no primitive, no HOST service, no native id, and no evaluator
 * change. See GLON-SHOP-G1A.md.
 */
#ifndef R0_S1_G1A_H
#define R0_S1_G1A_H

#include "s1.h"

/* Render a bundled fragment into out[0..cap). A fragment is a loader-heap
 * BLOCK of integer byte codes (0..255) which MAY contain <% ... %> embedded
 * Glon. On success returns 0 and sets *out_len to the rendered byte length
 * (the result is NUL-terminated when there is room). Returns -1 if `fragment`
 * is not a block or `cap` is too small. Never crashes on malformed input:
 * a `<%` with no closing `%>` is emitted as literal text, and an embedded
 * block that fails to parse is skipped. */
int r0_s1_g1a_render_fragment(cell fragment, char *out, int cap, int *out_len);

/* Route a token. Binds `current-route` to the token word, runs the GLON
 * `route` block (which selects a fragment), then renders the selected
 * fragment into out[0..cap). Returns 0 on success (HTML in out[], length via
 * *out_len); -1 on error (unsafe token, no fragment selected, or parse/run
 * failure). The application's routing decision and state mutation remain in
 * GLON (the `route` block); this function only supplies the token and invokes
 * the renderer. */
int r0_s1_g1a_route(const char *token, char *out, int cap, int *out_len);

/* Route an application EVENT token (G1C). Binds `current-event` to the token
 * word only (it does NOT assign `current-value` and does not require the
 * loaded program to define `mk-string`), runs the GLON `do-event` block (the
 * *application's* event dispatch), then renders the HTML that block produced.
 * Returns 0 on success, -1 on error. The host knows only that "an event
 * happened"; the meaning lives in GLON. */
int r0_s1_g1a_event(const char *token, char *out, int cap, int *out_len);

/* Route an application EVENT with a VALUE (G1D). Binds `current-event` to the
 * token word AND `current-value` to the value as the canonical managed STRING!
 * (built with the loaded program's `mk-string`), then runs the GLON `do-event`
 * block and renders the result. `value` is arbitrary text; the host forwards it
 * opaquely and GLON interprets it. This is the ONE value representation for
 * value-bearing events (no byte-list fallback). Returns 0 / -1. */
int r0_s1_g1a_event_value(const char *token, const char *value,
                          char *out, int cap, int *out_len);

#endif /* R0_S1_G1A_H */
