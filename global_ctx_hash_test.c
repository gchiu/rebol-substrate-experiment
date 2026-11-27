/* global_ctx_hash_test.c - GLON-CORE-C1 global-context hash regression.
 *
 * The P5 hash index stores entry = word_id * R0S1_HASH_RADIX + slot.  The radix
 * is the global-context capacity (R0S1_GLOBAL_CAP), so the constrained 64K
 * reference keeps the original encoding word_id*256 + slot exactly, and an
 * expanded profile with more bindings stays correct.  (With a fixed radix of 256
 * and a 512-binding context, slot s >= 256 was stored as (id+1)*256 + (s-256):
 * the word missed -- an "unbound word" fail-stop -- and the next word could
 * resolve to the wrong value.)
 *
 * Built once per profile by `make global-ctx-hash-test`:
 *   constrained 64K  -- R0S1_GLOBAL_CAP 256  / R0S1_SYM_CAP 512  (defaults)
 *   large            -- 512 / 1024 and 4096 / 8192
 * Each build fills the global context to its capacity and checks:
 *   W  every occupied hash cell equals word_id*radix + slot, names the word
 *      stored at that slot, and every slot is indexed exactly once
 *      (white-box; in the constrained build, radix == 256 is asserted);
 *   L  every binding resolves to its own value through the evaluator,
 *      including slots >= 256 when the capacity allows them;
 *   R  rebinding a high slot updates it in place;
 *   F  a function bound at a high slot can be called;
 *   C  the binding after the last free slot is a clean CONTEXT_FULL
 *      resource error, and the context is unchanged by it. */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char prog[16384];

static void fresh(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}

static int run(const char *src, r0_s1_outcome *oc) {
    r0_s1_session_run(src, (unsigned)strlen(src), oc);
    return oc->status == R0S1_OUT_OK;
}

static int run_int(const char *src, long *v) {
    r0_s1_outcome oc;
    if (!run(src, &oc) || oc.count < 1) return 0;
    cell r = r0_s1_result(oc.count - 1, oc.count);
    if (r0_tag(r) != T_INT) return 0;
    *v = (long)int_val(r);
    return 1;
}

/* Define hx-<i>: 100000+i for i in [from, to), 100 set-words per cell. */
static int define_range(long from, long to) {
    for (long base = from; base < to; base += 100) {
        int k = 0;
        for (long i = base; i < base + 100 && i < to; i++)
            k += snprintf(prog + k, sizeof prog - (size_t)k, "hx-%ld: %ld\n", i, 100000 + i);
        r0_s1_outcome oc;
        if (!run(prog, &oc)) return 0;
    }
    return 1;
}

/* W: every occupied hash cell decodes to (word_id, slot) with the word at that
 * slot, and each slot below count is indexed exactly once. */
static int hash_index_exact(long *entries, long *maxe) {
    cell ctx = M[GC_GLOBAL_CTX];
    cell p = r0_untag(ctx);
    cell count = M[p + CTX_COUNT], cap = M[p + CTX_CAP];
    cell ho = CTX_DATA + 2 * cap;
    char *seen = calloc((size_t)cap, 1);
    int ok = seen != NULL;
    *entries = 0; *maxe = -1;
    for (cell i = 0; ok && i < cap; i++) {
        cell e = M[p + ho + i];
        if (e == HASH_EMPTY) continue;
        if (e < 0) { ok = 0; break; }
        cell id = e / R0S1_HASH_RADIX, slot = e % R0S1_HASH_RADIX;
        if (slot >= count) { ok = 0; break; }
        if (word_id(M[p + CTX_DATA + 2 * slot]) != id) { ok = 0; break; }
        if (e != id * R0S1_HASH_RADIX + slot) { ok = 0; break; }
        if (seen[slot]++) { ok = 0; break; }
        (*entries)++;
        if (e > *maxe) *maxe = (long)e;
    }
    for (cell s = 0; ok && s < count; s++) if (!seen[s]) ok = 0;
    free(seen);
    return ok;
}

int main(void) {
    printf("global-ctx-hash-test: R0S1_GLOBAL_CAP=%ld R0S1_SYM_CAP=%ld R0S1_HASH_RADIX=%ld\n",
           (long)R0S1_GLOBAL_CAP, (long)R0S1_SYM_CAP, (long)R0S1_HASH_RADIX);
    printf("  layout: managed heap [%ld, %ld) loader [%ld, %ld) M1 arena %ld task table %ld"
           " DS %ld RS %ld\n",
           (long)GC_HEAP_BASE, (long)GC_HEAP_LIMIT, (long)R0S1_LOAD_BASE, (long)R0S1_HEAP_LIMIT,
           (long)M1_ARENA_BASE, (long)M1_TASK_TABLE, (long)R0S1_DS_INIT, (long)R0S1_RS_INIT);
    fresh();

    cell count0, cap;
    r0_s1_global_usage(&count0, &cap);
    printf("  global context at start: %ld / %ld\n", (long)count0, (long)cap);
    CHECK(cap == R0S1_GLOBAL_CAP, "the global context has the profile capacity");
    CHECK(R0S1_HASH_RADIX == R0S1_GLOBAL_CAP, "the hash radix is the global-context capacity");
    /* GLON-VIEW-CORE-V5: the reference identity is checked in the reference
     * build itself (no generated profile header).  A profile with 256 globals
     * may declare more symbols (server: 1,024): the encoding word_id*256 + slot
     * is the same, and the W check below verifies it cell by cell. */
#if R0S1_GLOBAL_CAP == 256 && !defined(GLON_PROFILE_NAME)
    CHECK(R0S1_HASH_RADIX == 256 && R0S1_SYM_CAP == 512,
          "constrained reference: radix 256, 512 symbols (the original encoding)");
#endif

    /* leave room for the four test helpers defined below */
    long n = (long)(R0S1_GLOBAL_CAP - count0 - 4);
    CHECK(define_range(0, n), "fill the global context to capacity - 4 with set-words");
    cell count1;
    r0_s1_global_usage(&count1, &cap);
    printf("  global context after fill: %ld / %ld (highest slot %ld)\n",
           (long)count1, (long)cap, (long)count1 - 1);
    CHECK(count1 == count0 + n, "every set-word made a new global binding");

    long entries, maxe;
    int exact = hash_index_exact(&entries, &maxe);
    printf("  hash index: %ld entries, largest entry %ld (limit 2147483647)\n", entries, maxe);
    CHECK(exact && entries == (long)count1,
          "W: every hash cell is word_id*radix + slot, each slot indexed exactly once");
#if R0S1_GLOBAL_CAP == 256
    CHECK(exact, "W: constrained entries are exactly word_id*256 + slot");
#endif

    long bad = 0, first_bad = -1;
    for (long i = 0; i < n; i++) {
        long v = 0;
        snprintf(prog, sizeof prog, "hx-%ld", i);
        if (!run_int(prog, &v) || v != 100000 + i) { if (first_bad < 0) first_bad = i; bad++; }
    }
    if (bad) printf("  %ld wrong; first hx-%ld\n", bad, first_bad);
    CHECK(bad == 0, R0S1_GLOBAL_CAP > 256
          ? "L: every binding resolves to its own value (slots below and above 256)"
          : "L: every binding resolves to its own value");

    long v = 0;
    snprintf(prog, sizeof prog, "hx-%ld: 7", n - 1);
    r0_s1_outcome oc;
    run(prog, &oc);
    snprintf(prog, sizeof prog, "hx-%ld", n - 1);
    CHECK(run_int(prog, &v) && v == 7, "R: rebinding the highest slot updates it in place");
    snprintf(prog, sizeof prog, "hx-%ld", n - 2);
    CHECK(run_int(prog, &v) && v == 100000 + n - 2, "R: the neighbouring slot is unaffected");

    CHECK(run("hx-inner: func [x] [ + x 1 ]\n"
              "hx-install: func [] [ hx-setup: func [] [ hx-inner 41 ] hx-setup ]", &oc),
          "F: define a nested installer in the last free slots");
    CHECK(run_int("hx-install", &v) && v == 42, "F: call it -> 42");

    /* fill the remaining slots exactly, then one more */
    cell count2;
    r0_s1_global_usage(&count2, &cap);
    long remain = (long)(cap - count2);
    int filled = 1;
    for (long i = 0; i < remain; i++) {
        snprintf(prog, sizeof prog, "hz-%ld: %ld", i, i);
        if (!run(prog, &oc)) filled = 0;
    }
    cell count3;
    r0_s1_global_usage(&count3, &cap);
    CHECK(filled && count3 == cap, "C: the context fills to exactly its capacity");
    CHECK(hash_index_exact(&entries, &maxe) && entries == (long)cap,
          "C: the full index is still exact");
    run("hz-overflow: 1", &oc);
    CHECK(oc.status == R0S1_OUT_RESOURCE_ERROR && oc.detail == R0S1_DETAIL_CONTEXT_FULL,
          "C: one binding more is a clean CONTEXT_FULL resource error");
    cell count4;
    r0_s1_global_usage(&count4, &cap);
    CHECK(count4 == cap && hash_index_exact(&entries, &maxe),
          "C: the refused binding left the context unchanged");

    printf("global-ctx-hash-test (cap %ld): %s (%d failure%s)\n", (long)R0S1_GLOBAL_CAP,
           failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
