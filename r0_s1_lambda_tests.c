/* r0_s1_lambda_tests.c - a lambda factory built purely in Glon.
 *
 * The experiment asks whether the existing Glon machinery (FUNC + nested
 * closures + GET) can manufacture a lambda family -- lambda0/lambda1/lambda2/
 * lambda3 and a general `lambda spec body` -- WITHOUT adding a native/C/runtime
 * primitive, a parser special case, or new syntax.
 *
 * Answer:
 *   - FUNC evaluates its spec and body arguments, so a factory can hand it
 *     spec/body blocks obtained from variables -- via GET (`:spec`, `:body`).
 *   - The factory manufactures arity-0..N closures, and the evaluator's
 *     "Guard of Binding" preserves an already-bound body's lexical denotation.
 *
 * Binding rule (precise):
 *   When runtime FUNC receives a lexically bound body whose originating
 *   activation is present in the current lexical ancestry, FUNC captures that
 *   activation's live child context and adds a +1 depth bias, so the body's
 *   bound words keep denoting what they denoted at the factory's call site.
 *
 * Explicit limits of the representation:
 *   - a bound BLOCK is not itself a closure; it carries lexical site/depth
 *     information, not a captured activation;
 *   - FUNC (not the block) creates the closure and captures the live lexical
 *     environment at construction time;
 *   - if the originating activation is no longer live/reachable through the
 *     frame walk, FUNC does NOT reconstruct it (dead-origin binding is out of
 *     scope for this milestone).
 */

#include "r0_s1.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static char src[65536];
static int N;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

/* parse + run a snippet in the persistent machine (the lambda library already
 * loaded in it). N = result arity; -1 on parse error. */
static void eval(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { N = -1; return; }
    N = r0_s1_run_persistent(b);
}

static int got(int i) { return (i < N) ? (int)int_val(r0_s1_result(i, N)) : 0; }

int run_r0_s1_lambda_tests(void) {
    r0_s1_init();

    printf("R0-S1 lambda factory (pure Glon)\n");

    /* ---- the factory (pure Glon, one factored mechanism) ----------------- */
    eval("[ lambda: func [spec body] [ func :spec :body ] "
         "  lambda0: func [body] [ lambda [] body ] "
         "  lambda1: func [body] [ lambda [a] body ] "
         "  lambda2: func [body] [ lambda [a b] body ] "
         "  lambda3: func [body] [ lambda [a b c] body ] ]");
    CHECK(N == 1 && r0_s1_ran_cleanly(), "library loads cleanly");

    /* the GC trigger is a raw; define it in its own load so the raw's assembly
     * does not interact with the closure site-id assignment above. */
    {
        char c[128];
        snprintf(c, sizeof c, "[ collect: raw [ CALL %ld ARITY 0 EXIT ] ]",
                 (long)r0_s1_gc_collect_addr());
        eval(c);
        CHECK(N == 1 && r0_s1_ran_cleanly(), "collect raw loads cleanly");
    }

    /* ---- arity 0 / 1 / 2 / 3 and general arity --------------------------- */
    eval("[ f: lambda0 [ 42 ]  f ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "1: lambda0 -> arity-0 callable (42)");

    eval("[ f: lambda1 [ + a 1 ]  f 41 ]");
    CHECK(N == 1 && got(0) == 42, "2: lambda1 -> arity-1 callable (41+1=42)");

    eval("[ f: lambda2 [ values [a b] ]  f 10 20 ]");
    CHECK(N == 2 && got(0) == 10 && got(1) == 20, "3: lambda2 -> arity-2 args in order (10 20)");

    eval("[ f: lambda3 [ values [a b c] ]  f 1 2 3 ]");
    CHECK(N == 3 && got(0) == 1 && got(1) == 2 && got(2) == 3, "4: lambda3 -> arity-3 args in order (1 2 3)");

    eval("[ f: lambda [a b c d] [ values [a b c d] ]  f 1 2 3 4 ]");
    CHECK(N == 4 && got(0) == 1 && got(1) == 2 && got(2) == 3 && got(3) == 4,
          "5: general lambda [a b c d] -> arity-4 args in order");

    /* ---- a body with no free words is just a block ----------------------- */
    eval("[ f: lambda0 [ 7 ]  g: lambda0 [ 9 ]  values [ f g ] ]");
    CHECK(N == 2 && got(0) == 7 && got(1) == 9, "6: two independent arity-0 closures");

    /* ---- recursion through a generated function --------------------------- */
    eval("[ fib: lambda1 [ either < a 2 [ a ] [ + fib - a 1 fib - a 2 ] ]  fib 10 ]");
    CHECK(N == 1 && got(0) == 55, "7: recursion through a generated function (fib 10 = 55)");

    /* ---- generated closures survive forced GC ---------------------------- */
    eval("[ f: lambda0 [ 42 ]  collect  collect  f ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "8: generated closure survives forced GC");

    /* ---- FUNC retains a reference to the body block (not a copy) --------- */
    eval("[ block-set!: raw 3 [ LIT SCRATCH_C ! LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT SCRATCH_A ! LIT SCRATCH_C @ LIT SCRATCH_A @ LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD ! ARITY 0 EXIT ] "
         "  block-at: raw 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD @ ARITY 1 EXIT ] "
         "  b: [ 1 ]  f: lambda0 [ block-at b 0 ]  block-set! b 0 99  f ]");
    CHECK(N == 1 && got(0) == 99, "9: body block is retained by reference (mutation visible)");

    /* ---- repeated construction does not corrupt the heaps ---------------- */
    eval("[ loop: func [i] [ either > i 0 [ f: lambda0 [ i ]  loop - i 1 ] [ 42 ] ]  loop 50 ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly() && !r0_s1_stack_sentry_fired(),
          "10: 50 constructions leave the heaps + sentries clean");

    /* ---- the Guard of Binding: capture of an enclosing local --------------- */
    eval("[ mk: func [start] [ lambda0 [ start: + start 1 start ] ]  a: mk 0  b: mk 100  values [a a b a b] ]");
    CHECK(N == 5 && got(0) == 1 && got(1) == 2 && got(2) == 101 && got(3) == 3 && got(4) == 102,
          "11: two generated counters capture independent enclosing locals (1 2 101 3 102)");

    /* arity-1 generated closure capturing an enclosing local */
    eval("[ mk: func [off] [ lambda1 [ + a off ] ]  g: mk 100  h: mk 1  values [g 5 h 5] ]");
    CHECK(N == 2 && got(0) == 105 && got(1) == 6, "12: arity-1 capture of distinct enclosing offsets (105 6)");

    /* nested: a lambda1 body capturing an enclosing local */
    eval("[ mk: func [n] [ lambda1 [ + a n ] ]  f: mk 77  f 5 ]");
    CHECK(N == 1 && got(0) == 82, "13: nested factory capture (5 + 77 = 82)");

    /* a generated body referring to a global */
    eval("[ base: 10  f: lambda1 [ + a base ]  f 5 ]");
    CHECK(N == 1 && got(0) == 15, "14: generated body refers to a global (5 + 10 = 15)");

    /* captured state survives forced GC while two closures stay live */
    eval("[ mk: func [start] [ lambda0 [ start: + start 1 start ] ]  a: mk 0  b: mk 100  collect  collect  values [a b a b] ]");
    CHECK(N == 4 && got(0) == 1 && got(1) == 101 && got(2) == 2 && got(3) == 102,
          "15: captured state survives forced GC (1 101 2 102)");

    /* ---- same-site re-entrant ancestry: the nearest activation wins ------ */
    /* make-at-depth recurses to depth n; each activation carries its own
     * `value`. At depth 0 the factory manufactures a closure from a body
     * lexically bound to make-at-depth's site. Several activations of that ONE
     * site are live at once, so r_binding_ctx must walk past the nearer
     * lambda0 frame and select the innermost make-at-depth activation (value
     * 30 for a, 25 for b), not the outermost or the first same-site frame. */
    eval("[ make-at-depth: func [n value] [ either > n 0 [ make-at-depth - n 1 + value 10 ] [ lambda0 [ value ] ] ] "
         "  a: make-at-depth 3 0  b: make-at-depth 2 5  values [a b a b] ]");
    CHECK(N == 4 && got(0) == 30 && got(1) == 25 && got(2) == 30 && got(3) == 25,
          "16: same-site re-entrant capture selects the nearest activation (30 25 30 25)");

    /* same, but with forced GC between construction and invocation (the
     * captured child contexts are promoted to the managed heap and must stay
     * live after the recursive calls have unwound). */
    eval("[ make-at-depth: func [n value] [ either > n 0 [ make-at-depth - n 1 + value 10 ] [ lambda0 [ value ] ] ] "
         "  a: make-at-depth 3 0  b: make-at-depth 2 5  collect  collect  values [a b a b] ]");
    CHECK(N == 4 && got(0) == 30 && got(1) == 25 && got(2) == 30 && got(3) == 25,
          "17: same-site re-entrant capture survives forced GC (30 25 30 25)");

    if (failures == 0) printf("all lambda-factory tests passed\n");
    return failures;
}
