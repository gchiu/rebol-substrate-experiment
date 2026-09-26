/* r0_s1_g1a_live.h -- experimental GLON_LIVE host-boundary layer.
 *
 * This header is ONLY for the experimental Live Translate build.  It adds a
 * generic outbound host capability request and a raw byte inbound event on top
 * of the existing G1A dispatcher, without changing S1, the parser, the
 * evaluator, the frozen HOST opcodes, or the existing r0_s1_g1a.c API.
 *
 *   outbound:  Glon renders a fragment whose first byte is 0x01, followed by
 *              "op" bytes, a 0x00 separator, and "arg" bytes.  The live
 *              dispatcher recognises that fragment, copies op/arg into host
 *              buffers, and calls the registered callback (the WASM build's
 *              callback invokes env.host_call(op, arg)).
 *
 *   inbound:   r0_s1_g1a_live_event_bytes() binds current-value to a managed
 *              STRING! built directly from raw bytes and runs do-event, so an
 *              asynchronous host reply can exceed the old 200-byte
 *              glon_event_value cap and carry arbitrary UTF-8.
 *
 * The existing r0_s1_g1a_* API is untouched: the live build uses these
 * functions, every other build keeps the original four-import/macros.
 */
#ifndef R0_S1_G1A_LIVE_H
#define R0_S1_G1A_LIVE_H

#include "s1.h"

/* called when Glon renders a host-call request fragment. op/arg point at
 * NUL-free byte buffers owned by the live layer and valid only for the call. */
typedef void (*r0_s1_g1a_live_host_call_fn)(const char *op, int op_len,
                                            const char *arg, int arg_len,
                                            void *user);

void r0_s1_g1a_live_set_host_call(r0_s1_g1a_live_host_call_fn fn, void *user);

/* Dispatch equivalents for the live build; same 0/-1 contract as the base
 * r0_s1_g1a_route/event/event_value, except a recognised host-call request
 * yields a successful dispatch with *out_len = 0 (no HTML to write). */
int r0_s1_g1a_live_route(const char *token, char *out, int cap, int *out_len);
int r0_s1_g1a_live_event(const char *token, char *out, int cap, int *out_len);
int r0_s1_g1a_live_event_value(const char *token, const char *value,
                               char *out, int cap, int *out_len);

/* raw byte inbound: data[0..dlen) becomes current-value as a managed STRING!
 * (built with the loaded program's mk-string/s/+), then do-event runs. */
int r0_s1_g1a_live_event_bytes(const char *token,
                               const unsigned char *data, unsigned int dlen,
                               char *out, int cap, int *out_len);

#endif /* R0_S1_G1A_LIVE_H */
