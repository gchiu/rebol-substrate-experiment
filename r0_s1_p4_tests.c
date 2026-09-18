/* r0_s1_p4_tests.c - FIB-OPT-P4: pre-resolved lexical (depth, slot) references.
 *
 * These are adversarial tests for the loader binding pass that converts
 * parameter references into bound words. Each test would FAIL if the binding
 * pass chose the wrong lexical slot, froze a dynamic/global name to a slot, or
 * confused slot identity with activation identity. Ordinary closures/natives
 * only (no datatypes / RAW / M3 machinery).
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

int run_r0_s1_p4_tests(void) {
    r0_s1_init();

    printf("R0-S1 P4: pre-resolved lexical references (adversarial)\n");

    /* 1. Shadowing: a parameter shadows a global of the same name. */
    { cell v[2] = { mk_int(5), mk_int(100) };
      expectN("[ x: 100  f: func [x] [ x ]  values [ f 5  x ] ]",
              2, v, "shadowing: param x (slot) shadows global x; global still 100"); }

    /* 2. Nested closure captures a variable two levels outward. */
    expect1("[ a: func [x] [ func [y] [ func [z] [ + x + y z ] ] ]  f: a 1  g: f 2  values [ g 3 ] ]",
            mk_int(6), "nested closure: x,y,z captured at depths 2,1,0");

    /* 3. Captured mutation: two closures share one captured parameter; the
     * set-word path and the bound-read path must address the same slot. */
    expect1("[ make: func [n] [ get: func [] [ n ]  set: func [v] [ n: v ]  set 42  get ]  make 5 ]",
            mk_int(42), "captured mutation: set 42 then get reads the promoted binding");

    /* 4. Recursion: each activation's slot 0 is a distinct binding. */
    expect1("[ fact: func [n] [ either <= n 1 [ 1 ] [ * n fact - n 1 ] ]  values [ fact 5 ] ]",
            mk_int(120), "recursion: slot identity != activation identity (fact 5 == 120)");

    /* 5. Escaping closure / promotion: a captured context promoted from the
     * task stack to the managed heap is still reachable by depth/slot. */
    expect1("[ make: func [n] [ func [m] [ + n m ] ]  mk: func [] [ make 10 ]  add10: mk  values [ add10 5 ] ]",
            mk_int(15), "promotion: escaped captured n=10 survives (add10 5 == 15)");

    /* 6. Dynamic/global names: a global redefinition is observed at run time,
     * proving the reference was NOT frozen to a slot. */
    expect1("[ x: 10  f: func [] [ x ]  x: 20  values [ f ] ]",
            mk_int(20), "dynamic global: f sees the redefined x (not a frozen slot)");

    /* 7. Higher-order: a parameter bound to a closure is invoked through the
     * bound slot, then dispatched as an ordinary closure. */
    expect1("[ apply: func [f x] [ f x ]  inc: func [x] [ + x 1 ]  values [ apply :inc 41 ] ]",
            mk_int(42), "higher-order: bound closure parameter invoked (apply :inc 41 == 42)");

    if (failures == 0) printf("all R0-S1 P4 lexical tests passed\n");
    return failures;
}
