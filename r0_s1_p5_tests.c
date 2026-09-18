/* r0_s1_p5_tests.c - FIB-OPT-P5: hash-indexed dynamic lookup (adversarial).
 *
 * These verify that the per-context hash index is semantically transparent:
 * rebinding, shadowing, nesting, promotion, multiple contexts, dynamic
 * insertion and many-bindings lookups must all resolve to the same values the
 * ordered linear scan produced. Ordinary closures/natives only.
 */

#include "r0_s1.h"
#include <stdio.h>

static int failures = 0;

static int run_src(const char *src, int *N) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(b);
    return 0;
}

static void expectN(const char *src, int n, const cell *vals, const char *what) {
    int N; run_src(src, &N);
    int ok = (N == n);
    if (ok) for (int i = 0; i < n; i++) if (r0_s1_result(i, N) != vals[i]) ok = 0;
    if (ok) printf("  ok: %s\n", what);
    else {
        printf("  FAIL: %s (N=%d want %d; got", what, N, n);
        for (int i = 0; i < N && i < n; i++) printf(" %ld", (long)int_val(r0_s1_result(i, N)));
        printf(")\n");
        failures++;
    }
}

static void expect1(const char *src, cell want, const char *what) {
    cell v[1] = { want };
    expectN(src, 1, v, what);
}

int run_r0_s1_p5_tests(void) {
    r0_s1_init();

    printf("R0-S1 P5: hash-indexed dynamic lookup (adversarial)\n");

    /* Rebinding: the index maps symbol -> slot, never freezing a value. */
    expect1("[ foo: 10  x: foo  foo: 20  y: foo  values [ + x y ] ]",
            mk_int(30), "rebinding: x=10 then foo:20 -> y=20 (x+y == 30)");

    /* Shadowing: a parameter shadows a global of the same name. */
    { cell v[2] = { mk_int(5), mk_int(100) };
      expectN("[ x: 100  f: func [x] [ x ]  values [ f 5  x ] ]",
              2, v, "shadowing: param x shadows global x; global still 100"); }

    /* Nested contexts: a captured parameter resolves through the chain. */
    expect1("[ a: func [x] [ func [y] [ func [z] [ + x + y z ] ] ]  f: a 1  g: f 2  values [ g 3 ] ]",
            mk_int(6), "nested: x,y,z captured at depths 2,1,0");

    /* Promotion: an escaped captured context keeps a correct index. */
    expect1("[ make: func [n] [ func [m] [ + n m ] ]  mk: func [] [ make 10 ]  add10: mk  values [ add10 5 ] ]",
            mk_int(15), "promotion: escaped captured n=10 (add10 5 == 15)");

    /* Multiple contexts with the same name resolve independently. */
    { cell v[2] = { mk_int(1), mk_int(2) };
      expectN("[ mk: func [n] [ func [] [ n ] ]  a: mk 1  b: mk 2  values [ a  b ] ]",
              2, v, "independent: two contexts both bind n -> 1 and 2"); }

    /* Dynamic insertion after context creation: set-words update the index. */
    expect1("[ f: func [] [ x: 1  x ]  values [ f ] ]",
            mk_int(1), "dynamic insertion: local x: 1 then read x");

    /* Many globals: look up the first, middle and last binding. */
    { cell v[3] = { mk_int(100), mk_int(115), mk_int(131) };
      expectN("[ g0: 100  g1: 101  g2: 102  g3: 103  g4: 104  g5: 105  g6: 106  g7: 107 "
              " g8: 108  g9: 109  g10: 110  g11: 111  g12: 112  g13: 113  g14: 114  g15: 115 "
              " g16: 116  g17: 117  g18: 118  g19: 119  g20: 120  g21: 121  g22: 122  g23: 123 "
              " g24: 124  g25: 125  g26: 126  g27: 127  g28: 128  g29: 129  g30: 130  g31: 131 "
              " values [ g0  g15  g31 ] ]",
              3, v, "many globals (first/middle/last resolve)"); }

    /* A full-size child context (16 params) resolves every slot. */
    expect1("[ f: func [a b c d e f g h i j k l m n o p] [ + a + b + c + d + e + f + g + h + i + j + k + l + m + n + o p ] "
            " values [ f 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 ] ]",
            mk_int(16), "full child context: 16 params all resolve");

    if (failures == 0) printf("all R0-S1 P5 hash-lookup tests passed\n");
    return failures;
}
