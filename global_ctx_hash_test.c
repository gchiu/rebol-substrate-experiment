/* global_ctx_hash_test.c - regression test for GLON-S1-CLAUDE-D1.
 *
 * The P5 context hash index packed each entry as word_id*256 + slot, which
 * silently assumed that no context holds more than 256 bindings. Once the
 * global context capacity became configurable (MEM-M2.1, GLOBAL_CTX_CAP=512),
 * a binding at slot s >= 256 was stored as (word_id+1)*256 + (s-256):
 *   - the word itself then missed in the index; the global context has no
 *     parent, so the lookup returned -1 and the evaluator fail-stopped on an
 *     "unbound" word (the G3H task-mode `app/install` halt, reason 0);
 *   - the word with the next symbol id could resolve to slot s-256, i.e. a
 *     silently WRONG value.
 * The entry radix is now R0S1_HASH_RADIX (65536), asserted to exceed every
 * context capacity.
 *
 * A SEPARATE TEST BUILD (`make global-ctx-hash-test`): the frozen 64K map with
 * -DGLOBAL_CTX_CAP=512, the capacity the expanded server profile uses. It
 * fills the global context well past 256 bindings and checks that every word
 * resolves to its own value, that set-words rebind high slots in place, and
 * that a function bound at a high slot (as `app/install` was) can be called. */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

#if !defined(GLOBAL_CTX_CAP) || GLOBAL_CTX_CAP <= 256
#error "build this test with make global-ctx-hash-test (-DGLOBAL_CTX_CAP=512)"
#endif

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char prog[8192];

/* Run `src` in the session; on success with one integer result store it. */
static int run_int(const char *src, long *v) {
    r0_s1_outcome oc;
    r0_s1_session_run(src, (unsigned)strlen(src), &oc);
    if (oc.status != R0S1_OUT_OK || oc.count < 1) return 0;
    cell r = r0_s1_result(oc.count - 1, oc.count);
    if ((r & 15) != T_INT) return 0;
    *v = (long)int_val(r);
    return 1;
}

static int run_ok(const char *src) {
    r0_s1_outcome oc;
    r0_s1_session_run(src, (unsigned)strlen(src), &oc);
    return oc.status == R0S1_OUT_OK;
}

#define NWORDS 400   /* fills slots well past 256 (the runtime binds ~110 first) */

int main(void) {
    printf("global-ctx-hash-test: GLOBAL_CTX_CAP=%d, R0S1_HASH_RADIX=%ld\n",
           (int)GLOBAL_CTX_CAP, (long)R0S1_HASH_RADIX);
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    cell count0, cap;
    r0_s1_global_usage(&count0, &cap);
    printf("  global context before: %ld / %ld\n", (long)count0, (long)cap);
    CHECK(cap == GLOBAL_CTX_CAP, "global context has the configured capacity");

    /* bind hx-0 .. hx-(NWORDS-1) = 1000+k, 100 set-words per cell (the
     * top-level block holds at most 512 elements) */
    int defined = 1;
    for (int base = 0; base < NWORDS; base += 100) {
        int k = 0;
        for (int i = base; i < base + 100 && i < NWORDS; i++)
            k += snprintf(prog + k, sizeof prog - (size_t)k, "hx-%d: %d\n", i, 1000 + i);
        if (!run_ok(prog)) defined = 0;
    }
    CHECK(defined, "400 global set-words run");
    cell count1;
    r0_s1_global_usage(&count1, &cap);
    printf("  global context after: %ld / %ld\n", (long)count1, (long)cap);
    CHECK(count1 == count0 + NWORDS, "every set-word made a new global binding");
    CHECK(count1 > 256 + 100, "bindings reach slots well past 256");

    /* every word resolves to its own value (no unbound halt, no alias) */
    int bad = 0, first_bad = -1;
    for (int i = 0; i < NWORDS; i++) {
        long v = 0;
        snprintf(prog, sizeof prog, "hx-%d", i);
        if (!run_int(prog, &v) || v != 1000 + i) { if (first_bad < 0) first_bad = i; bad++; }
    }
    if (bad) printf("  %d words wrong; first hx-%d\n", bad, first_bad);
    CHECK(bad == 0, "all 400 globals resolve to their own value (slots < and >= 256)");

    /* rebinding a high slot updates it in place */
    long v = 0;
    CHECK(run_ok("hx-399: 7") && run_int("hx-399", &v) && v == 7,
          "set-word rebinds a high-slot global in place");
    cell count2;
    r0_s1_global_usage(&count2, &cap);
    CHECK(count2 == count1, "rebinding made no new binding");
    CHECK(run_int("hx-398", &v) && v == 1398, "the neighbouring word is unaffected");

    /* a function bound at a high slot, calling nested functions bound at
     * high slots (the shape of the G3H app/install installer) */
    CHECK(run_ok("hx-inner: func [x] [ + x 1 ]\n"
                 "hx-install: func [] [ hx-setup: func [] [ hx-inner 41 ] hx-setup ]"),
          "define an installer at a high slot");
    CHECK(run_int("hx-install", &v) && v == 42, "call the high-slot installer -> 42");
    /* natives bound first (low slots) still resolve */
    CHECK(run_int("+ hx-0 hx-1", &v) && v == 2001, "low-slot natives and globals still resolve");

    printf("global-ctx-hash-test: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
