/* r0_s1_masm_bound_tests.c - the masm code-space bound and the data-stack
 * sentry's code boundary.
 *
 * Regression for two defects found by experiments/glon-masm (2026-11-09):
 *   1. The masm assembler emitted code past the data-stack boundary without an
 *      error: mem/ v002 followed by the glon-masm variants reached 16,932, into
 *      the return stack.
 *   2. The stack sentry compared SP with the runtime's own code end, captured at
 *      initialisation, not with the end of the masm library code loaded since.
 * The repair: the parser computes a masm block's exact emitted size (raw_size)
 * and refuses, before emitting anything, a block that would end above
 * R0S1_CODE_LIMIT = R0S1_DS_INIT - R0S1_DS_RESERVE; the sentry compares the
 * final SP and the deepest SP recorded during the run (RV_SPMIN) with the
 * current end of emitted code (asm_here()).
 *
 * Code of exact sizes is built from 3-cell (LIT 0 DROP) and 4-cell (LIT 0 @
 * DROP) units around a 21-cell tail (LIT 0 ARITY 1 EXIT).
 */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static char src[1 << 17];
static int N;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

#define POISON ((cell)0x5A5A5A5)

static void fresh(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}
/* parse and run; answers 0 on a clean run, -1 on a parse failure, -2 otherwise */
static int eval(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { N = -1; return -1; }
    N = r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -2;
}
static long got(void) { return (N >= 1) ? (long)int_val(r0_s1_result(N - 1, N)) : -999; }
static int load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  (cannot open %s)\n", path); return -3; }
    size_t n = fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
    char *r = src, *w = src;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) return -1;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -2;
}

/* "[ name: masm 0 [ ... ] ]" emitting exactly `cells` (>= 21, cells - 21 not 1, 2 or 5) */
static int word_src(char *out, size_t cap, const char *name, long cells) {
    long e = cells - 21, b = 0;
    while (e - 4 * b >= 0 && (e - 4 * b) % 3 != 0) b++;
    if (e < 0 || e - 4 * b < 0) return -1;
    long a = (e - 4 * b) / 3;
    int n = snprintf(out, cap, "[ %s: masm 0 [ ", name);
    for (long i = 0; i < a; i++) n += snprintf(out + n, cap - (size_t)n, "LIT 0 DROP ");
    for (long i = 0; i < b; i++) n += snprintf(out + n, cap - (size_t)n, "LIT 0 @ DROP ");
    n += snprintf(out + n, cap - (size_t)n, "LIT 0 ARITY 1 EXIT ] ]");
    return 0;
}
static int representable(long e) { return e == 0 || e == 3 || e == 4 || e >= 6; }
/* fill code to exactly R0S1_CODE_LIMIT with words fill-0, fill-1, ...; answers
 * how many words were made (or -1) */
static int fill_to_limit(void) {
    static char w[1 << 15];
    int k = 0;
    for (;;) {
        long room = R0S1_CODE_LIMIT - (long)asm_here();
        if (room == 0) return k;
        long size;
        if (room > 420) size = 400;
        else if (representable(room - 21)) size = room;
        else {      /* a small word first, so that what remains can be filled exactly */
            size = 0;
            for (long s = 24; s < 40; s++)
                if (representable(s - 21) && representable(room - s - 21)) { size = s; break; }
            if (!size) return -1;
        }
        char name[32]; snprintf(name, sizeof name, "fill-%d", k);
        if (word_src(w, sizeof w, name, size)) return -1;
        cell before = asm_here();
        strcpy(src, w);
        if (eval(w) != 0 || asm_here() - before != size) return -1;
        k++;
    }
}
static unsigned long checksum(cell a, cell b) {
    unsigned long h = 1469598103UL;
    for (cell c = a; c < b; c++) h = (h ^ (unsigned long)M[c]) * 1099511628211UL;
    return h;
}
static void poison_stacks(void) { for (cell c = R0S1_CODE_LIMIT; c < R0S1_RS_INIT; c++) M[c] = POISON; }
static int stacks_poisoned(void) {
    for (cell c = R0S1_CODE_LIMIT; c < R0S1_RS_INIT; c++) if (M[c] != POISON) return 0;
    return 1;
}

int run_r0_s1_masm_bound_tests(void) {
    printf("R0-S1 masm code-space bound and data-stack sentry\n");
    char w[4096];

    /* ---- 1-6: fill to the limit, then refuse ------------------------------ */
    fresh();
    CHECK(eval("[ inc: masm 1 [ LIT 16 ADD ARITY 1 EXIT ] twice: func [x] [ inc inc x ] ]") == 0,
          "0: a masm word and a function using it load");
    int words = fill_to_limit();
    CHECK(words > 0 && asm_here() == R0S1_CODE_LIMIT,
          "1: masm code filling the code region exactly up to R0S1_CODE_LIMIT is accepted");
    eval("[ fill-0 ]");
    CHECK(N >= 1 && got() == 0, "1: the first filler word runs");

    cell here0 = asm_here();
    unsigned long sum0 = checksum(256, here0);
    cell used0, cap; r0_s1_global_usage(&used0, &cap);
    poison_stacks();
    word_src(w, sizeof w, "over", 24);
    CHECK(eval(w) == -1 && r0_s1_parse_error_kind() == R0S1_PARSE_TOO_LARGE,
          "2: a 24-cell word past the limit is refused as a parse failure (too_large)");
    CHECK(eval("[ tiny: masm 0 [ LIT 0 ARITY 1 EXIT ] ]") == -1,
          "2: even the smallest word (21 cells) is refused when no room remains");
    CHECK(stacks_poisoned(), "3: refusal writes nothing to the data-stack reserve or the return stack");
    CHECK(checksum(256, here0) == sum0, "4: refusal changes no existing code (runtime and libraries)");
    CHECK(asm_here() == here0, "6: the code cursor is unchanged by the refusals");
    cell used1; r0_s1_global_usage(&used1, &cap);
    CHECK(used1 == used0, "6: no global binding was created by the refused definitions");
    CHECK(eval("[ twice 5 ]") == 0 && got() == 7, "5: an earlier masm word still runs (twice 5 = 7)");
    char last[64]; snprintf(last, sizeof last, "[ fill-%d ]", words - 1);
    CHECK(eval(last) == 0 && got() == 0, "5: the last filler word, ending at the limit, still runs");

    /* ---- 6b: a source whose SECOND masm block does not fit ----------------
     * Until 2026-11-09 the first block's code stayed in place (the frozen
     * assembler cannot move back), unreachable. masm blocks are now emitted
     * only when the whole source has parsed, so the refused source emits
     * nothing at all. */
    fresh();
    {
        static char big[1 << 15];
        for (;;) {   /* fill to exactly limit - 30 */
            long room = R0S1_CODE_LIMIT - 30 - (long)asm_here();
            if (room == 0) break;
            long size = room > 420 ? 400 : room;
            if (!representable(size - 21)) size = 24;
            word_src(big, sizeof big, "pad", size);
            if (eval(big) != 0) break;
        }
        CHECK(asm_here() == R0S1_CODE_LIMIT - 30, "6b: code filled to 30 cells below the limit");
        r0_s1_global_usage(&used0, &cap);
        cell before = asm_here();
        CHECK(eval("[ a1: masm 0 [ LIT 0 LIT 0 DROP ARITY 1 EXIT ] "
                   "  a2: masm 0 [ LIT 0 DROP LIT 0 DROP LIT 0 DROP LIT 0 ARITY 1 EXIT ] a3: 5 ]") == -1,
              "6b: a source whose second masm block does not fit is refused");
        r0_s1_global_usage(&used1, &cap);
        CHECK(asm_here() == before, "6b: the refused source emitted no code (not even its first block)");
        CHECK(used1 == used0, "6b: no binding from the refused source (not a1, a2 or a3)");
        CHECK(eval("[ fits: masm 0 [ LIT 0 ARITY 1 EXIT ] fits ]") == 0 && got() == 0,
              "6b: the 30 cells left are all still usable (a 21-cell word fits and runs)");
    }

    /* ---- 7: data-stack growth into loaded code is prevented --------------- */
    /* nested 15-argument calls: each level holds 14 arguments on the data
     * stack while the next level is evaluated. Since the data-stack guard
     * (R0S1_DS_GUARD, 2026-11-09) the evaluator fail-stops before the stack
     * reaches the end of emitted code; before it, the post-run sentry detected
     * the growth after the code had been overwritten. */
    static char nest[1 << 15];
    const char *g = "[ g: func [a b c d e f h i j k l m n o p] [ 0 ] ]";
    int depth_small = 6, depth_big = 30;

    fresh();
    eval(g);
    {
        int n = snprintf(nest, sizeof nest, "[ ");
        for (int d = 0; d < depth_big; d++) n += snprintf(nest + n, sizeof nest - (size_t)n, "g 1 1 1 1 1 1 1 1 1 1 1 1 1 1 ");
        n += snprintf(nest + n, sizeof nest - (size_t)n, "0 ]");
    }
    eval(nest);
    CHECK(!r0_s1_stack_sentry_fired(), "7: control: the deep nesting is legal when the code region is small");

    fresh();
    eval(g);
    fill_to_limit();
    {
        int n = snprintf(nest, sizeof nest, "[ ");
        for (int d = 0; d < depth_small; d++) n += snprintf(nest + n, sizeof nest - (size_t)n, "g 1 1 1 1 1 1 1 1 1 1 1 1 1 1 ");
        n += snprintf(nest + n, sizeof nest - (size_t)n, "0 ]");
    }
    eval(nest);
    CHECK(!r0_s1_stack_sentry_fired(), "7: control: a shallow nesting fits the data-stack reserve below full code");
    {
        int n = snprintf(nest, sizeof nest, "[ ");
        for (int d = 0; d < depth_big; d++) n += snprintf(nest + n, sizeof nest - (size_t)n, "g 1 1 1 1 1 1 1 1 1 1 1 1 1 1 ");
        n += snprintf(nest + n, sizeof nest - (size_t)n, "0 ]");
    }
    {
        cell top = asm_here(); unsigned long before = checksum(256, top);
        eval(nest);
        CHECK(!r0_s1_ran_cleanly() && r0_s1_halt_reason() == R0S1_HALT_DS_EXHAUSTED,
              "7: data-stack growth past the reserve towards loaded masm code fail-stops (data-stack exhaustion)");
        CHECK(checksum(256, top) == before,
              "7: ... before writing any code cell (runtime and masm libraries unchanged)");
        CHECK(eval("[ fill-0 ]") == 0 && got() == 0, "7: the session continues: a library word still runs");
    }

    /* ---- 8: several masm libraries; the original failing sequence ---------- */
    fresh();
    int ok = load("jupyter/prelude.glon") == 0 && load("demo/shop/strings.glon") == 0
          && load("demo/shop/strings-ext.glon") == 0;
    CHECK(ok, "8: prelude, strings and strings-ext load");
    CHECK(load("experiments/glon-mem/mem.glon") == 0 && asm_here() <= R0S1_CODE_LIMIT,
          "8: the mem/ library (1,577 code cells) loads within the limit");
    CHECK(eval("[ mm-r: mem/alloc 10 mem/set-byte mm-r 3 77 mem/byte mm-r 3 ]") == 0 && got() == 77,
          "8: a mem/ region works");
    r0_s1_global_usage(&used0, &cap);
    cell here1 = asm_here();
    unsigned long sum1 = checksum(256, here1);
    poison_stacks();
    CHECK(load("experiments/glon-masm/variants.glon") == -1 && r0_s1_parse_error_kind() == R0S1_PARSE_TOO_LARGE,
          "8: the glon-masm variants after mem/ (which reached 16,932) are refused");
    CHECK(stacks_poisoned(), "8: refusing them writes nothing at or above the code limit");
    r0_s1_global_usage(&used1, &cap);
    CHECK(used1 == used0, "8: no variant was bound");
    CHECK(checksum(256, here1) == sum1 && asm_here() <= R0S1_CODE_LIMIT,
          "8: the refusal changes no code loaded before it, and the cursor stays at or below the limit");
    CHECK(eval("[ mem/byte mm-r 3 ]") == 0 && got() == 77, "8: the mem/ library still works after the refusal");
    CHECK(eval("[ small: masm 1 [ LIT 32 ADD ARITY 1 EXIT ] small 1 ]") == 0 && got() == 3,
          "8: a small library that fits the remaining space still loads and runs");
    CHECK(!r0_s1_stack_sentry_fired(), "8: no stack sentry in any of these runs");

    printf("  %d failure(s)\n", failures);
    return failures;
}
