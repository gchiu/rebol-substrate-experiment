/* r0_s1_case_tests.c - the closure-origin binding law, and CASE as ordinary
 * Glon vocabulary (demo/shop/case.glon) built on it.
 *
 * Part 1 pins the Guard of Binding for closures made from blocks: a literal
 * func body binds to the new func's own scope; a computed body binds to its
 * block's LIVE lexical origin (the origin activation, or the origin captured by
 * a factory closure made from one of its blocks); a top-level block binds to
 * the global context; a dead origin fail-stops rather than resolving against
 * an unrelated context. Promotion shares one context between the activation
 * and its closures, and never leaves a fresh closure unrooted across a GC.
 *
 * Part 2 exercises CASE (demo/shop/case.glon, loaded after bootstrap):
 * first-match-only, lazy conditions/actions, results,
 * no-match, malformed input, and lexical correctness inside functions,
 * closures, recursion and nested CASEs.
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
    cell save_lhp = M[GC_LOADER_HP];
    cell b = r0_s1_parse(src, &err);
    if (err) { M[GC_LOADER_HP] = save_lhp; N = -1; return; }
    N = r0_s1_run_persistent(b);
    M[GC_LOADER_HP] = save_lhp;
}

static int got(int i) { return (i < N) ? (int)int_val(r0_s1_result(i, N)) : 0; }
static int one(int v) { return N == 1 && got(0) == v && r0_s1_ran_cleanly(); }
static int none1(void) { return N == 1 && r0_s1_result(0, N) == R0_NONE && r0_s1_ran_cleanly(); }

int run_r0_s1_case_tests(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    printf("R0-S1 closure-origin binding law + CASE vocabulary\n");

    /* CASE depends on the shared block-len/block-at vocabulary */
    CHECK(load_file("demo/shop/bootstrap.glon") == 0, "bootstrap loads");
    CHECK(load_file("demo/shop/case.glon") == 0, "case.glon loads");

    /* ---- Part 1: closure-origin binding law ------------------------------ */

    /* a func spec names NEW parameters: a same-named enclosing parameter must
     * not capture it (was: g 3 read the outer x = 5, giving 55). */
    eval("[ f: func [x] [ g: func [x] [ x ]  + * 10 x g 3 ]  f 5 ]");
    CHECK(one(53), "B1: inner func param shadows same-named outer param (53)");

    /* a literal func body binds to its own fresh scope even while an older
     * activation of that same literal is live (was: captured the old
     * activation with +1 bias, reading m=8/n=1 -> 108). */
    eval("[ k: 0  f: func [n] [ g: func [m] [ k: + k 1  either = k 1 [ f 0 ] [ + * 100 n m ] ]  g + n 7 ]  f 1 ]");
    CHECK(one(7), "B2: literal func re-evaluated under its own live activation (7)");

    /* dead origin: a block returned from a finished activation, wrapped into a
     * closure, must fail-stop (was: silently read a stale slot). */
    eval("[ mk: func [x] [ [x] ]  b: mk 7  t: does b  t ]");
    CHECK(!r0_s1_ran_cleanly(), "B3: closure over a dead-origin bound block fails loudly");
    eval("[ mk: func [x] [ [x] ]  b: mk 7  t: does b  w: func [a b c] [ t ]  w 11 22 33 ]");
    CHECK(!r0_s1_ran_cleanly(), "B3b: ... also when invoked under an unrelated activation");

    /* a closure made inside an escaped factory closure binds to that
     * closure's captured origin (was: no bias-0 frame -> stale slot). */
    eval("[ mk: func [x] [ does [ t: does [x] t ] ]  g: mk 5  w: func [a b] [ g ]  w 66 77 ]");
    CHECK(one(5), "B4: closure inside an escaped closure sees its captured origin (5)");
    eval("[ f: func [x g] [ either = g none [ does [ t: does [x] t ] ] [ g ] ]  h: f 5 none  f 9 h ]");
    CHECK(one(5), "B5: ... not a later live activation of the same site (5, not 9)");

    /* promotion through a factory shares ONE context both ways
     * (was: the closure got a snapshot copy). */
    eval("[ f: func [x] [ y: 1  g: does [ y ]  y: 2  g ]  f 0 ]");
    CHECK(one(2), "B6: activation's later assignment visible to its closure (2)");
    eval("[ f: func [x] [ g: does [ x: 9 ]  g  x ]  f 0 ]");
    CHECK(one(9), "B6b: closure's assignment visible to its activation (9)");

    /* a top-level block's origin is the global context: a factory's own
     * parameters (lambda's spec/body) must not capture its words. */
    eval("[ body: 5  h: does [ body ]  h ]");
    CHECK(one(5), "B7: top-level block via a factory is not captured by its params (5)");

    /* a computed body taken from the current activation's own block */
    eval("[ f: func [x] [ b: [x]  t: func [] b  t ]  f 5 ]");
    CHECK(one(5), "B8: func over the running activation's own block (5)");

    /* a helper that builds a closure from its caller's block: the helper's own
     * context is left intact (was: RV_CTX overwritten, then corrupt). */
    eval("[ th: func [b] [ func [] b ]  f: func [x] [ t: th [x]  + x 100 ]  f 5 ]");
    CHECK(one(105), "B9: closure built in a helper from the caller's block (105)");
    eval("[ th: func [b q] [ t: func [] b  q ]  f: func [x] [ th [x] 44 ]  f 5 ]");
    CHECK(one(44), "B9b: helper's own parameters still correct after promotion (44)");

    eval("[ t: func [] 5 ]");
    CHECK(!r0_s1_ran_cleanly(), "B10: func with a non-block body fails loudly");

    /* GC: a closure whose creation promotes a context must survive a
     * collection triggered by that promotion (was: swept -> freed object). */
    eval("[ alive?: masm 1 [ DUP LIT 16 MOD SUB LIT 15 SUB @ LIT 2 MOD LIT 16 MUL ARITY 1 EXIT ] "
         "  gcs: masm [ LIT 25040 @ LIT 16 MUL ARITY 1 EXIT ] "
         "  mkg: func [x] [ g: func [] [x]  alive? :g ] "
         "  run: func [n acc] [ either = n 0 [acc] [ run - n 1 + acc mkg n ] ] "
         "  g0: gcs  a: + + run 60 0 run 60 0 run 60 0  + * 1000 a - gcs g0 ]");
    CHECK(N == 1 && got(0) / 1000 == 180 && got(0) % 1000 > 0 && r0_s1_ran_cleanly(),
          "B11: 180/180 fresh closures stay allocated across promotion-triggered GCs");

    /* ---- Part 2: CASE ------------------------------------------------------ */

    const char *sign = "sgn: func [x] [ case [ [> x 0] [x] [= x 0] [100] ] ] ";
    char buf[1024];
    snprintf(buf, sizeof buf, "[ %s sgn 7 ]", sign);   eval(buf);
    CHECK(one(7), "C1: first branch taken, action sees the function's x (7)");
    snprintf(buf, sizeof buf, "[ %s sgn 0 ]", sign);   eval(buf);
    CHECK(one(100), "C2: second branch taken (100)");
    snprintf(buf, sizeof buf, "[ %s sgn -3 ]", sign);  eval(buf);
    CHECK(none1(), "C3: no match returns NONE");

    /* n counts every evaluated condition/action: cond1 (false), cond2 (true),
     * action2 -> n = 3; action1, cond3, action3 never run. */
    eval("[ n: 0  bump: func [v] [ n: + n 1  v ] "
         "  r: case [ [bump 0] [bump 10] [bump 1] [bump 20] [bump 1] [bump 30] ] "
         "  + * 100 r n ]");
    CHECK(one(2003), "C4: first match only; later conditions/actions not evaluated (20, n=3)");

    eval("[ case [] ]");
    CHECK(none1(), "C5: empty CASE returns NONE");
    eval("[ case [ [0] [1] [none] [2] ] ]");
    CHECK(none1(), "C6: 0 and NONE are falsey");
    eval("[ case [ [1] [1 2 3] ] ]");
    CHECK(one(3), "C7: branch result is the action block's result (last value, 3)");
    eval("[ case [ [1] [] ] ]");
    CHECK(none1(), "C8: empty action yields NONE");
    eval("[ case [ [1] ] ]");
    CHECK(!r0_s1_ran_cleanly(), "C9: condition without an action fails loudly");
    eval("[ case [ 1 [2] ] ]");
    CHECK(!r0_s1_ran_cleanly(), "C10: non-block condition fails loudly");

    /* CASE's own parameter names (cases, i) never capture the caller's words */
    eval("[ cases: 5  i: 6  x: 9  case [ [1] [ + x + cases i ] ] ]");
    CHECK(one(20), "C11: top-level CASE sees globals, not CASE internals (20)");
    eval("[ f: func [cases i] [ case [ [1] [ + cases i ] ] ]  f 30 4 ]");
    CHECK(one(34), "C12: caller params named like CASE internals (34)");

    eval("[ f: func [x] [ g: func [x] [ case [ [1] [x] ] ]  + * 10 x g 3 ]  f 5 ]");
    CHECK(one(53), "C13: CASE in a nested func with a shadowed x (53)");
    eval("[ f: func [x] [ g: func [y] [ case [ [> y x] [y] [1] [x] ] ]  + * 100 g 9 g 2 ]  f 5 ]");
    CHECK(one(905), "C14: CASE mixing inner param and captured outer param (905)");

    eval("[ mk: func [x] [ does [ case [ [> x 0] [x] [1] [-1] ] ] ] "
         "  g: mk 5  h: mk -2  w: func [x] [ + * 10 g h ]  w 99 ]");
    CHECK(one(49), "C15: CASE inside escaped closures, called under an unrelated x (49)");

    eval("[ f: func [x] [ y: 1  case [ [1] [ y: 7 ] ]  y ]  f 0 ]");
    CHECK(one(7), "C16: assignment to a local in an action is visible after CASE (7)");
    eval("[ f: func [x] [ case [ [1] [ x: 8 ] ]  x ]  f 0 ]");
    CHECK(one(8), "C16b: assignment to a parameter in an action is visible (8)");

    /* pinned limitation: an action runs as a zero-argument closure, so RETURN
     * returns from the action (CASE yields 1) and f continues to 2. */
    eval("[ f: func [x] [ case [ [> x 0] [ return 1 ] ]  2 ]  f 5 ]");
    CHECK(one(2), "C17: RETURN inside an action returns from the action (pinned: 2)");

    eval("[ mk: func [x] [ [ [1] [x] ] ]  b: mk 7  case b ]");
    CHECK(!r0_s1_ran_cleanly(), "C18: CASE over a dead-origin block fails loudly");

    eval("[ f: func [x d] [ either = d 0 [ f 99 1 ] [ case [ [1] [x] ] ] ]  f 5 0 ]");
    CHECK(one(99), "C19: CASE under same-site recursion uses the writing activation (99)");

    eval("[ f: func [x] [ case [ [ case [ [> x 1] [1] ] ] [ case [ [> x 5] [x] [1] [0] ] ] [1] [-1] ] ] "
         "  + * 100 f 9 f 3 ]");
    CHECK(one(900), "C20: nested CASE in conditions and actions (900)");

    eval("[ fib: func [n] [ case [ [< n 2] [n] [1] [ + fib - n 1 fib - n 2 ] ] ]  fib 15 ]");
    CHECK(one(610), "C21: recursive function whose body is a CASE (fib 15 = 610)");

    /* each live `run` level holds its promoted context (CASE's closures
     * capture it), so keep `run` shallow and repeat it to churn the heap. */
    eval("[ gcs: masm [ LIT 25040 @ LIT 16 MUL ARITY 1 EXIT ] "
         "  run: func [n acc] [ either = n 0 [acc] "
         "      [ run - n 1 + acc case [ [> n 5] [2] [1] [1] ] ] ] "
         "  rep: func [k acc] [ either = k 0 [acc] [ rep - k 1 + acc run 10 0 ] ] "
         "  g0: gcs  a: rep 20 0  + * 1000 a - gcs g0 ]");
    CHECK(N == 1 && got(0) / 1000 == 300 && got(0) % 1000 > 0 && r0_s1_ran_cleanly(),
          "C22: CASE results stay correct across GC cycles (20 x 15 = 300)");

    if (failures == 0) printf("all closure-origin binding + CASE tests passed\n");
    return failures;
}
