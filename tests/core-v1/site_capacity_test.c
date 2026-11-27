#define _POSIX_C_SOURCE 200809L
/* GLON-VIEW-CORE-V1: function-site capacity per memory profile (SITE-1 on C2).
 * Ported from view/tests/test_site_capacity.c (glon-gui-f2 6fc3f404), runtime
 * only: no View host.  Built once per profile (make site-capacity-test).
 *
 * A fresh session takes func literals up to exactly its capacity
 * (SITE_PARENT_CAP - 1 sites: ids start at 1), past the 64K reference's 640
 * where the profile allows; functions defined below and above 640 run,
 * closures among them (their escape-law checks walk the site-parent table);
 * one more literal is a clean SITE_TABLE_FULL parse error that takes no site;
 * the session still runs code afterwards. */
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef GLON_PROFILE_NAME
#define GLON_PROFILE_NAME "constrained"
#endif

#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); failures++; } } while (0)
static int failures;

static cell used(void) { cell u = 0, cap = 0; r0_s1_site_usage(&u, &cap); return u; }
static cell capacity(void) { cell u = 0, cap = 0; r0_s1_site_usage(&u, &cap); return cap; }

/* parse and run src; 0 on success, the parse error kind (> 0) or -1 */
static int run(const char *src) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) return r0_s1_parse_error_kind() > 0 ? r0_s1_parse_error_kind() : -1;
    int n = r0_s1_run_persistent(prog);
    return (r0_s1_ran_cleanly() && n >= 0) ? 0 : -1;
}
static long int_of(const char *expr) {
    char src[256];
    snprintf(src, sizeof src, "[ %s ]", expr);
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) return -999999;
    int n = r0_s1_run_persistent(prog);
    if (!r0_s1_ran_cleanly() || n < 1) return -999999;
    cell v = r0_s1_result(0, n);
    return (v & 15) == T_INT ? (long)int_val(v) : -999998;
}
/* define functions sf<start> .. sf<start+count-1>, one site each:
 * sfN: func [x] [ + x N ] */
static int define(int start, int count) {
    char *src = malloc((size_t)count * 48 + 16);
    if (!src) return -1;
    int n = sprintf(src, "[ ");
    for (int i = 0; i < count; i++) n += sprintf(src + n, "sf%d: func [x] [ + x %d ] ", start + i, start + i);
    sprintf(src + n, "]");
    int rc = run(src);
    free(src);
    return rc;
}

/* k literal sites, parsed only (no closure is made): `func [] 1`, one site
 * and one 16-cell spec block each (as r0_s1_escape_law_tests.c does) */
static int parse_sites(int k) {
    char *src = malloc((size_t)k * 12 + 16);
    if (!src) return -1;
    int n = sprintf(src, "[ ");
    for (int i = 0; i < k; i++) n += sprintf(src + n, "func [] 1 ");
    sprintf(src + n, "]");
    int perr = 0;
    r0_s1_parse(src, &perr);
    free(src);
    return perr ? r0_s1_parse_error_kind() : 0;
}

static int bare(void) {
    r0_s1_init();
    cell cap = capacity();
    long limit = (long)cap - 1;
    printf("site capacity %ld (64K reference: 640); a fresh session uses %ld\n", (long)cap, (long)used());
    CHECK(used() == 0, "a fresh session has used no sites");
    CHECK(cap >= 640, "the capacity is at least the 64K reference's 640");
    /* functions that run: 50 in the 64K profile (its heap), 700 elsewhere
     * (past the reference's 640) */
    int defined = limit > 700 ? 700 : 50;
    int ok = 1;
    for (int at = 1; at <= defined && ok; at += 50) ok = define(at, 50) == 0;
    char what[160];
    snprintf(what, sizeof what, "%d functions defined and loaded", defined);
    CHECK(ok && used() == defined, what);
    CHECK(int_of("sf1 41") == 42, "the first site's function runs");
    if (defined == 700)
        CHECK(int_of("sf639 1") == 640 && int_of("sf640 1") == 641 && int_of("sf641 1") == 642,
              "functions at sites 639, 640, 641 (around the old limit) run");
    /* the rest of the table, parsed only, up to one short of the capacity */
    ok = 1;
    while (ok && used() < limit - 1) {
        int k = limit - 1 - used() > 100 ? 100 : (int)(limit - 1 - used());
        ok = parse_sites(k) == 0;
    }
    /* the last site: a function that runs */
    CHECK(ok && define(900000, 1) == 0 && used() == limit && int_of("sf900000 1") == 900001,
          "the table fills to its capacity; the function at the last site runs");
    snprintf(what, sizeof what, "%ld sites used: the capacity less site 0", limit);
    CHECK(used() == limit, what);
    /* one more: a clean parse error, no site taken, the session unharmed */
    int rc = run("[ over: func [x] [ x ] ]");
    CHECK(rc == R0S1_PARSE_SITE_TABLE_FULL, "one more func literal is a SITE_TABLE_FULL parse error");
    CHECK(used() == limit, "the failed parse took no site");
    CHECK(run("[ plain: 7 ]") == 0 && int_of("plain") == 7, "code without func literals still loads and runs");
    rc = run("[ a: func [] [ 1 ] b: func [] [ 2 ] ]");
    CHECK(rc == R0S1_PARSE_SITE_TABLE_FULL && used() == limit, "a two-literal source is refused as a whole");
    CHECK(int_of("sf2 1") == 3, "earlier functions still run after the refusals");
    /* the 64K reference itself: still exactly 640 */
#if SITE_PARENT_BASE == 32024
    CHECK(cap == 640, "the fixed (64K reference) site table keeps 640 sites at 32024");
#else
    CHECK(SITE_PARENT_BASE >= GC_HEAP_LIMIT && SITE_PARENT_BASE + SITE_PARENT_CAP <= R0S1_LOAD_BASE,
          "the profile site table lies between the managed heap / worklist and the loader");
#endif
    return 0;
}

/* closures whose escape-law checks walk the site-parent table, made in a
 * session whose ids are past 640 (desktop profiles) */
static int closures_high(void) {
    r0_s1_init();
    if (capacity() <= 700) return 0;
    int ok = define(1, 100) == 0 && define(101, 100) == 0 && define(201, 100) == 0 && define(301, 100) == 0 &&
             define(401, 100) == 0 && define(501, 100) == 0 && define(601, 100) == 0;
    CHECK(ok && used() == 700, "700 sites loaded (past the 64K reference)");
    /* sites 701..: a closure factory and nested functions */
    int rc = run("[ make-adder: func [n] [ func [x] [ + x n ] ]  add5: make-adder 5 "
                 "twice: func [f x] [ f f x ]  outer: func [a] [ inner: func [b] [ + a b ]  inner 10 ] ]");
    CHECK(rc == 0 && used() > 700, "closures defined at sites above 700");
    CHECK(int_of("add5 1") == 6, "a closure returned from its factory (site > 700) runs");
    CHECK(int_of("twice :add5 1") == 11, "a closure passed and called twice runs");
    CHECK(int_of("outer 1") == 11, "a nested function (its parent site > 700) runs");
    return 0;
}

int main(void) {
    printf("profile %s: site table [%ld, +%ld)\n", GLON_PROFILE_NAME, (long)SITE_PARENT_BASE, (long)SITE_PARENT_CAP);
    bare();
    closures_high();
    printf("SITE_CAPACITY %s %s (%d failure%s)\n", GLON_PROFILE_NAME, failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
