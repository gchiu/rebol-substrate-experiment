/* r0_s1_parse_tests.c - minimal structural PARSE (block matching).
 *
 * PARSE is pure Glon vocabulary in demo/shop/parse.glon, loaded separately from
 * bootstrap. The Alpha rule grammar is DATA: operators skip/alt/opt/some/any
 * (bare words) plus literal values matched with `=`. INTO is deliberately NOT
 * implemented (it needs a general block? predicate the core does not yet have).
 *
 * parse input rules -> 1 iff the rules succeed AND consume the whole input.
 */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static char src[65536];
static int N;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(src, 1, sizeof src - 1, f);
    fclose(f);
    src[n] = 0;
    strip_comments(src);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) return -2;
    int r = r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly()) return -4;
    return (r < 0) ? -3 : 0;
}

static void eval(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    /* reclaim the throw-away test source (loader-neutral, like eval_int) */
    cell save_lhp = M[GC_LOADER_HP];
    cell b = r0_s1_parse(src, &err);
    if (err) { M[GC_LOADER_HP] = save_lhp; N = -1; return; }
    N = r0_s1_run_persistent(b);
    M[GC_LOADER_HP] = save_lhp;
}

static void eval_persist(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { N = -1; return; }
    N = r0_s1_run_persistent(b);
}

static int got(int i) { return (i < N) ? (int)int_val(r0_s1_result(i, N)) : 0; }

int run_r0_s1_parse_tests(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    printf("R0-S1 minimal structural PARSE (block matching)\n");

    /* parse depends on the shared block-len/block-at vocabulary */
    CHECK(load_file("demo/shop/bootstrap.glon") == 0, "bootstrap loads");
    CHECK(load_file("demo/shop/parse.glon") == 0, "parse.glon loads");

    /* a forced-collect raw for the GC test (persistent: its [entry, arity]
     * block must survive subsequent reclaiming evals) */
    {
        char lib[256];
        snprintf(lib, sizeof lib, "[ collect: raw [ CALL %ld ARITY 0 EXIT ] ]",
                 (long)r0_s1_gc_collect_addr());
        eval_persist(lib);
    }

    eval("[ parse [] [] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "1: empty input / empty rules -> true");

    eval("[ parse [] [1] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "2: empty input / literal rule -> false");

    eval("[ parse [1 2 3] [1 2 3] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "3: exact literal sequence -> true");

    eval("[ parse [1 2 4] [1 2 3] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "4: literal mismatch -> false");

    eval("[ parse [1 2 3] [1 2] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "5: prefix-only rules -> false (full-match)");

    eval("[ parse [7 8 9] [skip skip skip] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "6: SKIP matches one value");

    eval("[ parse [1] [skip skip] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "7: SKIP at end of input fails");

    eval("[ parse [1] [alt [1] [2]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "8: ALT first branch succeeds");

    eval("[ parse [2] [alt [1] [2]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "9: ALT second branch succeeds after backtrack");

    eval("[ parse [3] [alt [1] [2]] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "10: ALT both branches fail");

    eval("[ parse [1 2] [1 opt [2]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "11: OPT present");

    eval("[ parse [1] [1 opt [2]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "12: OPT absent (succeeds without consuming)");

    eval("[ parse [1] [some [1]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "13: SOME one occurrence");

    eval("[ parse [1 1 1] [some [1]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "14: SOME several occurrences");

    eval("[ parse [] [some [1]] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "15: SOME zero occurrences fails");

    eval("[ parse [] [any [1]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "16: ANY zero occurrences");

    eval("[ parse [1 1 1] [any [1]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "17: ANY several occurrences");

    eval("[ parse [1 2 1 2] [some [alt [1] [2]]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "18: nested ALT inside repetition");

    /* zero-width repetition must terminate (progress guard) */
    eval("[ parse [] [any [opt [1]]] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "19: zero-width repetition terminates (progress guard)");

    eval("[ parse [2] [any [opt [1]]] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "19a: zero-width repetition consumes nothing");

    /* managed input produced by REDUCE */
    eval("[ data: reduce [1 2 3] parse data [1 2 3] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "20: managed input produced by REDUCE");

    /* word literals (bare words match by word identity) */
    eval("[ parse [BEGIN BUY SELL] [BEGIN BUY SELL] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "word literals match by identity");

    eval("[ parse [BEGIN SELL] [BEGIN BUY SELL] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "word literal mismatch");

    /* repeated parse calls must not leak loader state */
    {
        cell hp0 = M[GC_LOADER_HP];
        for (int i = 0; i < 8; i++) {
            eval("[ parse [1 2 3] [1 2 3] ]");
            eval("[ parse [1 2 4] [1 2 3] ]");
        }
        CHECK(M[GC_LOADER_HP] == hp0, "25: repeated parse calls do not leak loader state");
    }

    /* forced GC with managed input alive */
    eval("[ data: reduce [1 2 3] collect parse data [1 2 3] ]");
    CHECK(N == 1 && got(0) == 1 && r0_s1_ran_cleanly(), "26: forced GC with managed input alive");

    if (failures == 0) printf("all parse tests passed\n");
    return failures;
}
