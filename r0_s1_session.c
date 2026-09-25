/* r0_s1_session.c - run one source cell in a persistent session and classify
 * the outcome from structured runtime state.
 *
 * A session is one runtime (r0_s1_init once), into which cells are parsed and
 * run one after another with r0_s1_run_persistent, so definitions persist. This
 * facade adds nothing to the language: it only reads the structured state the
 * runtime already exposes (r0_s1_parse_error_kind, r0_s1_ran_cleanly,
 * r0_s1_uncaught_error, r0_s1_halt_reason, r0_s1_stack_sentry_fired,
 * r0_s1_result) and never inspects rendered text.
 *
 * A cell is source WITHOUT the outer [ ] (as in the primer and Saturnine):
 * `;;` line comments are stripped and the cell is wrapped as "[ src ]".
 *
 * No libc beyond what the runtime already uses. */

#include "r0_s1.h"

static char cellbuf[16400];

static void outcome_reset(r0_s1_outcome *out) {
    out->status = R0S1_OUT_OK;
    out->detail = R0S1_DETAIL_NONE;
    out->count = 0;
    out->sin_type = out->sin_id = out->sin_arg = R0_NONE;
}

/* classify the parse failure recorded by r0_s1_parse_error_kind(). Shared by
 * r0_s1_session_run and hosts that parse a whole source file themselves. */
void r0_s1_session_parse_error(r0_s1_outcome *out) {
    outcome_reset(out);
    switch (r0_s1_parse_error_kind()) {
    case R0S1_PARSE_SYMBOL_TABLE_FULL:
        out->status = R0S1_OUT_RESOURCE_ERROR; out->detail = R0S1_DETAIL_SYMBOL_TABLE_FULL; break;
    case R0S1_PARSE_LOADER_EXHAUSTED:
        out->status = R0S1_OUT_RESOURCE_ERROR; out->detail = R0S1_DETAIL_LOADER_EXHAUSTED; break;
    case R0S1_PARSE_SITE_TABLE_FULL:
        out->status = R0S1_OUT_RESOURCE_ERROR; out->detail = R0S1_DETAIL_SITE_TABLE_FULL; break;
    case R0S1_PARSE_TOO_LARGE:
        out->status = R0S1_OUT_PARSE_ERROR; out->detail = R0S1_DETAIL_TOO_LARGE; break;
    default:
        out->status = R0S1_OUT_PARSE_ERROR; out->detail = R0S1_DETAIL_SYNTAX; break;
    }
}

/* run an already-parsed program in the persistent session and classify the
 * outcome from structured runtime state (exactly as r0_s1_session_run does). */
int r0_s1_session_run_block(cell prog, r0_s1_outcome *out) {
    outcome_reset(out);

    int n = r0_s1_run_persistent(prog);
    cell t, i, a;
    if (r0_s1_stack_sentry_fired()) {
        out->status = R0S1_OUT_HALT; out->detail = R0S1_DETAIL_STACK_SENTRY;
    } else if (r0_s1_ran_cleanly() && n >= 0) {
        out->status = R0S1_OUT_OK; out->count = n;
    } else if (r0_s1_uncaught_error(&t, &i, &a)) {
        out->status = R0S1_OUT_UNCAUGHT_SIN;
        out->sin_type = t; out->sin_id = i; out->sin_arg = a;
    } else if (r0_s1_halt_reason() == R0S1_HALT_CONTEXT_FULL) {
        out->status = R0S1_OUT_RESOURCE_ERROR; out->detail = R0S1_DETAIL_CONTEXT_FULL;
    } else {
        out->status = R0S1_OUT_HALT; out->detail = R0S1_DETAIL_MACHINE;
    }
    return out->status;
}

int r0_s1_session_run(const char *src, unsigned int len, r0_s1_outcome *out) {
    outcome_reset(out);

    if (len + 4 >= sizeof cellbuf) {
        out->status = R0S1_OUT_PARSE_ERROR;
        out->detail = R0S1_DETAIL_TOO_LARGE;
        return out->status;
    }
    unsigned int w = 0, r = 0;
    cellbuf[w++] = '[';
    cellbuf[w++] = ' ';
    while (r < len) {
        if (src[r] == ';' && r + 1 < len && src[r + 1] == ';') {
            while (r < len && src[r] != '\n') r++;
            continue;
        }
        cellbuf[w++] = src[r++];
    }
    cellbuf[w++] = ' ';
    cellbuf[w++] = ']';
    cellbuf[w] = 0;

    int err = 0;
    cell prog = r0_s1_parse(cellbuf, &err);
    if (err) {
        r0_s1_session_parse_error(out);
        return out->status;
    }
    return r0_s1_session_run_block(prog, out);
}

const char *r0_s1_outcome_status_name(int status) {
    switch (status) {
    case R0S1_OUT_OK:             return "OK";
    case R0S1_OUT_PARSE_ERROR:    return "PARSE_ERROR";
    case R0S1_OUT_RESOURCE_ERROR: return "RESOURCE_ERROR";
    case R0S1_OUT_UNCAUGHT_SIN:   return "UNCAUGHT_SIN";
    case R0S1_OUT_HALT:           return "HALT";
    }
    return "UNKNOWN";
}

const char *r0_s1_outcome_detail_name(int detail) {
    switch (detail) {
    case R0S1_DETAIL_NONE:              return "none";
    case R0S1_DETAIL_SYNTAX:            return "syntax";
    case R0S1_DETAIL_TOO_LARGE:         return "too_large";
    case R0S1_DETAIL_SYMBOL_TABLE_FULL: return "symbol_table_full";
    case R0S1_DETAIL_LOADER_EXHAUSTED:  return "loader_exhausted";
    case R0S1_DETAIL_SITE_TABLE_FULL:   return "site_table_full";
    case R0S1_DETAIL_CONTEXT_FULL:      return "context_full";
    case R0S1_DETAIL_STACK_SENTRY:      return "stack_sentry";
    case R0S1_DETAIL_MACHINE:           return "machine";
    }
    return "unknown";
}
