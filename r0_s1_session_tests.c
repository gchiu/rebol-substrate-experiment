/* r0_s1_session_tests.c - the persistent-session facade (r0_s1_session_run)
 * classifies every outcome from structured runtime state.
 *
 * One runtime is initialised once; cells then run one after another in it (as
 * the native session host jupyter/host/glon_kernel_host.c does). Each check
 * reads only the structured outcome (status, detail, count, SIN! fields) and
 * molded values -- never rendered text. */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static r0_s1_outcome oc;
static char text[256];

static int run(const char *src) {
    return r0_s1_session_run(src, (unsigned)strlen(src), &oc);
}
/* OK with exactly one result that molds as `want` */
static int one(const char *src, const char *want) {
    if (run(src) != R0S1_OUT_OK || oc.count != 1) return 0;
    r0_s1_mold(r0_s1_result(0, oc.count), text, sizeof text);
    return strcmp(text, want) == 0;
}
static int is(int status, int detail) { return oc.status == status && oc.detail == detail; }

static char big[40000];

int run_r0_s1_session_tests(void) {
    printf("R0-S1 persistent session facade (structured outcomes)\n");

    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    CHECK(one("x: 21", "21") && one("* x 2", "42"),
          "S1: persistence: x: 21, then * x 2 -> 42 in the same runtime");
    CHECK(one("mk: func [n] [ func [] [ n: + n 1 ] ]  c: mk 0  c", "1") && one("c", "2"),
          "S2: closure state persists across cells (1, 2)");
    CHECK(one(";; comment\n\n  1  ;; trailing\n", "1"), "S3: ;; comments are stripped from a cell");
    CHECK(run("") == R0S1_OUT_OK && oc.count == 1 && one("", "none"), "S4: an empty cell is OK and yields none");

    CHECK(run("y: [ oops") == R0S1_OUT_PARSE_ERROR && is(R0S1_OUT_PARSE_ERROR, R0S1_DETAIL_SYNTAX),
          "S5: malformed source is PARSE_ERROR / syntax");
    {
        char *w = big;
        w += sprintf(w, "[ ");
        for (int i = 0; i < 600; i++) w += sprintf(w, "1 ");
        sprintf(w, "]");
        run(big);
        CHECK(is(R0S1_OUT_PARSE_ERROR, R0S1_DETAIL_TOO_LARGE), "S6: a 600-value block is PARSE_ERROR / too_large");
    }
    CHECK(run("raise create-sin 'demo 'oops 7") == R0S1_OUT_UNCAUGHT_SIN, "S7: an uncaught SIN! is UNCAUGHT_SIN");
    r0_s1_mold(oc.sin_type, text, sizeof text);
    int t_ok = !strcmp(text, "demo");
    r0_s1_mold(oc.sin_id, text, sizeof text);
    int i_ok = !strcmp(text, "oops");
    r0_s1_mold(oc.sin_arg, text, sizeof text);
    CHECK(t_ok && i_ok && !strcmp(text, "7"), "S8: its structured fields are type demo, id oops, arg 7");
    CHECK(one("judge [ raise create-sin 'demo 'oops 7 ]", "#[SIN! demo oops 7]"),
          "S9: a judged SIN! is an ordinary OK result");
    CHECK(run("undefined-word") == R0S1_OUT_HALT && is(R0S1_OUT_HALT, R0S1_DETAIL_MACHINE),
          "S10: an unset word is HALT / machine");
    CHECK(one("* x 2", "42") && one("c", "3"), "S11: the session survives all of those (42, c -> 3)");

    CHECK(run("f: func [] [ a1: 1 a2: 2 a3: 3 a4: 4 a5: 5 a6: 6 a7: 7 a8: 8 a9: 9 a10: 10"
              " a11: 11 a12: 12 a13: 13 a14: 14 a15: 15 a16: 16 a17: 17  0 ]  f") == R0S1_OUT_RESOURCE_ERROR
          && is(R0S1_OUT_RESOURCE_ERROR, R0S1_DETAIL_CONTEXT_FULL),
          "S12: a full context is RESOURCE_ERROR / context_full (not a HALT)");
    {
        char *w = big;
        for (int b = 0; b < 7; b++) {
            w += sprintf(w, "[ ");
            for (int i = 0; i < 500; i++) w += sprintf(w, "[] ");
            w += sprintf(w, "] ");
        }
        run(big);
        CHECK(is(R0S1_OUT_RESOURCE_ERROR, R0S1_DETAIL_LOADER_EXHAUSTED),
              "S13: an oversized cell is RESOURCE_ERROR / loader_exhausted");
    }
    {
        char *w = big;
        for (int b = 0; b < 5; b++) {
            w += sprintf(w, "[ ");
            for (int i = 0; i < 140; i++) w += sprintf(w, "func [] 1 ");
            w += sprintf(w, "] ");
        }
        run(big);
        CHECK(is(R0S1_OUT_RESOURCE_ERROR, R0S1_DETAIL_SITE_TABLE_FULL),
              "S14: 700 func sites is RESOURCE_ERROR / site_table_full");
    }
    CHECK(one("* x 2", "42") && one("c", "4"), "S15: the session survives the resource errors too (42, c -> 4)");
    CHECK(!strcmp(r0_s1_outcome_status_name(R0S1_OUT_RESOURCE_ERROR), "RESOURCE_ERROR")
          && !strcmp(r0_s1_outcome_detail_name(R0S1_DETAIL_CONTEXT_FULL), "context_full"),
          "S16: status and detail names for hosts");

    if (failures == 0) printf("all session facade tests passed\n");
    return failures;
}
