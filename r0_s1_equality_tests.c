/* r0_s1_equality_tests.c - the Alpha equality law.
 *
 * "=" is type-safe identity:
 *   - WORD/SET/GET/LIT (tags 2..5) compare by symbol id, so a quoted word and a
 *     plain word denote the same symbol (route/event dispatch relies on this);
 *   - every other tag compares by raw cell identity (INT by value, NONE only
 *     with NONE, BLOCK/CLOSURE/STRING by address).
 *
 * This replaces the old numeric (int_val) "=" that made NONE == 0, WORD == INT,
 * and block == int. Content equality for strings is the separate str-eq word.
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

static void eval(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell save_lhp = M[GC_LOADER_HP];
    cell b = r0_s1_parse(src, &err);
    if (err) { M[GC_LOADER_HP] = save_lhp; N = -1; return; }
    N = r0_s1_run_persistent(b);
    M[GC_LOADER_HP] = save_lhp;
}

static int got(int i) { return (i < N) ? (int)int_val(r0_s1_result(i, N)) : 0; }

static void t(const char *prog, int want) {
    eval(prog);
    CHECK(N == 1 && got(0) == want && r0_s1_ran_cleanly(), prog);
}

int run_r0_s1_equality_tests(void) {
    r0_s1_init();

    printf("R0-S1 equality law (type-safe identity)\n");

    /* bootstrap gives mk-string / str-eq for the string cases */
    {
        FILE *f = fopen("demo/shop/bootstrap.glon", "rb");
        size_t n = fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
        strip_comments(src);
        int err = 0; cell b = r0_s1_parse(src, &err);
        if (!err) r0_s1_run_persistent(b);
    }

    t("[ = none none ]", 1);
    t("[ = none 0 ]", 0);
    t("[ = 0 0 ]", 1);
    t("[ = 1 0 ]", 0);
    t("[ = 5 5 ]", 1);
    t("[ = 5 6 ]", 0);
    t("[ = -1 -1 ]", 1);
    t("[ = 'alpha 'alpha ]", 1);
    t("[ = 'alpha 'beta ]", 0);
    t("[ a: 'alpha = a 'alpha ]", 1);        /* word vs lit-word: same symbol */
    t("[ b: [1 2] = b b ]", 1);              /* same block identity */
    t("[ = [1 2] [1 2] ]", 0);               /* distinct equal-content blocks */
    t("[ b: reduce [1 2] = b b ]", 1);       /* managed block identity */
    t("[ = reduce [1 2] [1 2] ]", 0);        /* managed vs loader block */
    t("[ f: does [42] = f f ]", 1);          /* closure identity */
    t("[ = does [42] does [42] ]", 0);       /* distinct closures */
    t("[ = \"abc\" \"abc\" ]", 0);           /* strings: identity, not content */
    t("[ str-eq \"abc\" \"abc\" ]", 1);      /* content equality is str-eq */

    /* a literal integer can no longer impersonate a word operator (symbol id
     * collisions): func/return/raw are ids 0/1/2. */
    t("[ = 0 'func ]", 0);
    t("[ = 1 'return ]", 0);
    t("[ = 2 'raw ]", 0);

    if (failures == 0) printf("all equality tests passed\n");
    return failures;
}
