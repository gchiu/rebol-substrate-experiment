/* r0_s1_escape_law_tests.c - hard regression corpus for the escape-time law.
 *
 * THE LAW (implemented and enforced unconditionally):
 *
 *   A bare activation-dependent block may be consumed or closed over while its
 *   originating activation is live, but it may not escape that activation.
 *   Persistent or travelling executable code must be a closure.
 *
 * "Activation-dependent" = the block (or a block nested in it, outside nested
 * literal funcs) contains a T_BOUND reference to an enclosing parameter, or a
 * RETURN (RETURN finds its target by BLK_SITE). Enforcement points:
 *   (1) frame exit      - a frame of site S (or of a site nested in S) must not
 *                         return / RETURN-unwind an activation-dependent block
 *                         of site S;
 *   (2) argument bind   - such a block of site S must not be bound as an
 *                         argument of a closure whose site is S;
 *   (3) context store   - a set-word must not store it into a context not owned
 *                         by site S or a site nested in S (incl. the global one);
 *   (4) capture         - a context must not be captured (promoted) while it
 *                         holds such a block of a foreign site;
 *   (5) RAW/containers  - raw transport is outside the guarantee; blessed
 *                         container/task words must check or taint.
 *
 * STRUCTURE. Each case runs in a fresh runtime. Every case is a HARD check:
 *   - the 10 legal cases must succeed (value);
 *   - the 16 law cases must fail-stop at the named enforcement point.
 * A regression that lets an illegal escape succeed prints FAIL and fails the
 * suite. There is no xfail/XPASS/strict mode any more: the law is current.
 *
 * EXISTING COMMITTED COVERAGE (deliberately not duplicated here):
 *   r0_s1_bound_tests.c
 *     8  [ mk: func [x] [ [x] ]  b: mk 7  do b ]           fail-stop (use);
 *        law: fail-stop at (1) mk's frame exit. Assertion unchanged.
 *     11 [ f: func [x] [ do [ [x] ] ]  f 7 ]               was: succeed as inert
 *        data; NOW: fail-stop at (1) - the returned [x] escapes f even as data.
 *     13 same-site recursion `f 99 1 b` then `do carried`  was: 99 (wrong-origin);
 *        NOW: fail-stop at (2) at argument bind.
 *     6, 12 cross-function `do` in a foreign-site frame    fail-stop (LOAD-LEX).
 *     1, 2, 3, 5, 14                                        legal; unchanged.
 *   r0_s1_case_tests.c
 *     B3, B3b, C18 (dead-origin block closed over / CASEd) fail-stop (use);
 *        law: fail-stop earlier, at (1). Assertions unchanged.
 *     B4, B5, B8, B9 and the other C cases                   legal; unchanged.
 *
 * Case ids P05-P08, P30, P31, P35-P37, N2, N5, X1, X3, K1, K2 are the names of
 * the scratch probes from the binding-law review and escape censuses, reduced
 * here to minimal programs. CTX1 is the non-global foreign-context store; L1-L10
 * are legal controls.
 */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *M1_LIB;          /* task words: spawn / run-tasks (r0_s1_m1_tests.c) */

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

static int run_src(void) {
    int err = 0;
    strip_comments(src);
    cell b = r0_s1_parse(src, &err);
    if (err) return -1;
    N = r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -1;
}

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(src, 1, sizeof src - 1, f);
    fclose(f);
    src[n] = 0;
    return run_src();
}

/* fresh runtime: M1 task words (optional), bootstrap, CASE */
static int fresh(int tasks) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    if (tasks) {
        snprintf(src, sizeof src, "[ %s ]", M1_LIB);
        if (run_src() != 0) return -1;
    }
    if (load_file("demo/shop/bootstrap.glon") != 0) return -1;
    if (load_file("demo/shop/case.glon") != 0) return -1;
    return 0;
}

enum { VALUE, FAILSTOP };
typedef struct {
    const char *id, *what, *prog;
    int tasks;
    int cur;  long cur_v;       /* documented current behaviour (c1a036c) */
    int law;  long law_v;       /* required under the escape-time law */
    const char *point;          /* enforcement point that must fire */
} esc_case;

/* observed outcome of the last run: VALUE with an integer, else FAILSTOP */
static int observe(long *v) {
    if (r0_s1_ran_cleanly() && N == 1 && (r0_s1_result(0, N) & 15) == 0) {
        *v = (long)int_val(r0_s1_result(0, N));
        return VALUE;
    }
    *v = 0;
    return r0_s1_ran_cleanly() ? VALUE + 2 : FAILSTOP;   /* clean non-int: neither */
}

static const char *desc(int k, long v, char *buf) {
    if (k == VALUE) sprintf(buf, "value %ld", v);
    else if (k == FAILSTOP) strcpy(buf, "fail-stop");
    else strcpy(buf, "clean non-integer result");
    return buf;
}

static void run_case(const esc_case *c) {
    char b1[64], msg[512];
    if (fresh(c->tasks) != 0) { printf("  FAIL: %s setup\n", c->id); failures++; return; }
    snprintf(src, sizeof src, "%s", c->prog);
    run_src();
    long v; int k = observe(&v);
    int law_ok = (k == c->law) && (k != VALUE || v == c->law_v);
    if (c->law == FAILSTOP)
        snprintf(msg, sizeof msg, "%s %s -- fail-stop at %s", c->id, c->what, c->point);
    else
        snprintf(msg, sizeof msg, "%s %s -- %s", c->id, c->what, desc(c->law, c->law_v, b1));
    if (!law_ok) printf("       observed: %s\n", desc(k, v, b1));
    CHECK(law_ok, msg);
}

static const esc_case CASES[] = {
    /* ---- wrong-origin via escape: must fail-stop under the law ------------- */
    { "P05", "two closures from one literal: g5's block run inside g9",
      "[ mk: func [x] [ func [b] [ either = b none [ [x] ] [ do b ] ] ]"
      "  g5: mk 5  g9: mk 9  c: g5 none  g9 c ]",
      0, VALUE, 9, FAILSTOP, 0, "(1) exit of the g5 activation" },
    { "P06", "two closures from one literal: g5's clauses CASEd inside g9",
      "[ mk: func [x] [ func [b] [ either = b none [ [ [1] [x] ] ] [ case b ] ] ]"
      "  g5: mk 5  g9: mk 9  c: g5 none  g9 c ]",
      0, VALUE, 9, FAILSTOP, 0, "(1) exit of the g5 activation" },
    { "P07/X3", "dead-origin clause list CASEd in a later activation",
      "[ f: func [x b] [ either = b none [ [ [1] [x] ] ] [ case b ] ]  c: f 5 none  f 9 c ]",
      0, VALUE, 9, FAILSTOP, 0, "(1) exit of f 5" },
    { "P08", "dead-origin block closed over by does in a later activation",
      "[ f: func [x b] [ either = b none [ [x] ] [ t: does b  t ] ]  c: f 5 none  f 9 c ]",
      0, VALUE, 9, FAILSTOP, 0, "(1) exit of f 5" },
    { "P30", "recursion: clauses from f 10 passed into f 99 and CASEd there",
      "[ f: func [x cl] [ either = cl none [ f 99 [ [1] [ * x 2 ] ] ] [ case cl ] ]  f 10 none ]",
      0, VALUE, 198, FAILSTOP, 0, "(2) binding cl of f 99" },
    { "P31", "dead-origin block with a nested literal func reads a foreign slot",
      "[ mk: func [x] [ [ z: 7  g: func [y] [ + x y ]  g 1 ] ]  b: mk 70  t: does b  t ]",
      0, VALUE, 8, FAILSTOP, 0, "(1) exit of mk" },
    { "P35", "RETURN-only dependency: dead block's RETURN unwinds an unrelated mk",
      "[ mk: func [x t] [ either = t none [ [ return 7 ] ] [ t  99 ] ]"
      "  c: mk 1 none  tq: does c  mk 2 :tq ]",
      0, VALUE, 7, FAILSTOP, 0, "(1) exit of mk 1 (block contains RETURN)" },
    { "P36", "live writer's block passed into a same-site closure of another activation",
      "[ f: func [x g] [ either = :g none [ lambda [b] [ do b ] ] [ g [x] ] ]"
      "  h: f 5 none  f 9 :h ]",
      0, VALUE, 5, FAILSTOP, 0, "(2) binding b of h (site of f)" },
    { "P37", "live writer's clauses passed into a same-site closure that CASEs them",
      "[ f: func [x g] [ either = :g none [ lambda [cl] [ case cl ] ] [ g [ [1] [x] ] ] ]"
      "  h: f 5 none  f 9 :h ]",
      0, VALUE, 5, FAILSTOP, 0, "(2) binding cl of h (site of f)" },
    { "K1", "own block captured, then extracted by a nested literal closure",
      "[ f: func [x g] [ either = :g none [ b: [ [1] [x] ]  func [] [ b ] ] [ case g ] ]"
      "  h: f 5 none  f 9 :h ]",
      0, VALUE, 9, FAILSTOP, 0, "(1) exit of the nested closure (site nested in f)" },
    { "K2", "foreign helper captures the caller's block in a closure",
      "[ keeper: func [c] [ func [] [ c ] ]"
      "  f: func [x g] [ either = :g none [ keeper [ [1] [x] ] ] [ case g ] ]"
      "  h: f 5 none  f 9 :h ]",
      0, VALUE, 9, FAILSTOP, 0, "(4) capture of keeper's context" },
    { "X1", "store into the global context, closed over by a later activation",
      "[ gb: 0  f: func [x d] [ either = d 0 [ gb: [x]  0 ] [ t: does gb  t ] ]  f 5 0  f 9 1 ]",
      0, VALUE, 9, FAILSTOP, 0, "(3) global store in f 5" },
    { "X1b", "store into the global context, executed at top level",
      "[ gb: 0  f: func [x] [ gb: [x]  0 ]  f 5  do gb ]",
      0, FAILSTOP, 0, FAILSTOP, 0, "(3) global store" },
    { "CTX1", "store into an enclosing activation's context (non-global)",
      "[ f: func [] [ keep: none"
      "    g: func [x d] [ either = d 0 [ keep: [x]  0 ] [ t: does keep  t ] ]"
      "    g 5 0  g 9 1 ]  f ]",
      0, VALUE, 9, FAILSTOP, 0, "(3) store of g's block into f's context" },
    { "N5", "cross-task: dead-origin clauses CASEd by the same function in a task",
      "[ res: 0  f: func [x b] [ either = b none [ [ [1] [x] ] ] [ res: case b ] ]"
      "  cl: f 5 none  spawn [ f 9 cl ]  run-tasks  res ]",
      1, VALUE, 9, FAILSTOP, 0, "(1) exit of f 5" },
    { "N2", "cross-task: bound task body escapes its spawning activation",
      "[ res: 0  go: func [x] [ spawn [ res: x ] ]  go 5  run-tasks  res ]",
      1, FAILSTOP, 0, FAILSTOP, 0, "(5) task transport (mnew-task ESC_ANY, before any task-table change)" },

    /* ---- legal: must succeed now and under the law ---------------------------- */
    { "L1", "downward CASE in the writing activation",
      "[ f: func [x] [ case [ [> x 0] [x] [1] [0] ] ]  f 5 ]",
      0, VALUE, 5, VALUE, 5, "-" },
    { "L2", "downward does, used while the origin is live",
      "[ f: func [x] [ t: does [ + x 1 ]  t ]  f 5 ]",
      0, VALUE, 6, VALUE, 6, "-" },
    { "L3", "downward lambda, used while the origin is live",
      "[ f: func [x] [ h: lambda [y] [ + x y ]  h 2 ]  f 5 ]",
      0, VALUE, 7, VALUE, 7, "-" },
    { "L4", "clauses passed down to an other-site helper that CASEs them",
      "[ run: func [cl] [ case cl ]  f: func [x] [ run [ [1] [ * x 2 ] ] ]  f 10 ]",
      0, VALUE, 20, VALUE, 20, "-" },
    { "L5", "ordinary recursion through CASE",
      "[ fib: func [n] [ case [ [< n 2] [n] [1] [ + fib - n 1 fib - n 2 ] ] ]  fib 10 ]",
      0, VALUE, 55, VALUE, 55, "-" },
    { "L6", "same-site recursion where each activation CASEs its own clauses",
      "[ f: func [x d] [ either = d 0 [ f 99 1 ] [ case [ [1] [x] ] ] ]  f 5 0 ]",
      0, VALUE, 99, VALUE, 99, "-" },
    { "L7", "a closure is the portable form: escapes and runs under another x",
      "[ mk: func [x] [ does [x] ]  h: mk 5  w: func [x] [ h ]  w 9 ]",
      0, VALUE, 5, VALUE, 5, "-" },
    { "L8", "own block kept in the origin's captured context, run by its closure",
      "[ mk: func [x] [ b: [x]  does [ do b ] ]  h: mk 5  h ]",
      0, VALUE, 5, VALUE, 5, "-" },
    { "L9", "an activation-independent data block may be returned",
      "[ f: func [x] [ [1 2 3] ]  block-len f 5 ]",
      0, VALUE, 3, VALUE, 3, "-" },
    { "L10", "unbound task body transported to a task after its spawner returned",
      "[ res: 0  go: func [] [ spawn [ res: 42 ] ]  go  run-tasks  res ]",
      1, VALUE, 42, VALUE, 42, "-" },
};

/* Focused regression for the SITE_PARENT_CAP bound: a source with more func
 * sites than the table can hold must fail to LOAD cleanly (parse error), never
 * silently clamp or overwrite the table. Each site is `func [] 1` (a computed
 * body: one site and one 16-cell spec block), so both sources fit the loader
 * heap and the site table is what is tested. (With `func [] [1]`, 32 loader
 * cells per site, 500 sites exceed the loader heap: that case used to "parse
 * cleanly" only because loader exhaustion was silent and wrote through M[-1].) */
static void test_site_capacity(void) {
    char *p;
    for (int over = 0; over < 2; over++) {
        int blocks = over ? 7 : 5;        /* 700 > CAP(640) sites; 500 < CAP */
        r0_s1_init();
        p = src; p += sprintf(p, "[");
        for (int bx = 0; bx < blocks; bx++) {
            p += sprintf(p, "[ ");
            for (int i = 0; i < 100; i++) p += sprintf(p, "func [] 1 ");
            p += sprintf(p, "] ");
        }
        p += sprintf(p, "]");
        int err = 0;
        strip_comments(src);
        r0_s1_parse(src, &err);
        if (over)
            CHECK(err != 0 && r0_s1_parse_error_kind() == R0S1_PARSE_SITE_TABLE_FULL,
                  "site-table: 700 func sites (> CAP 640) is a clean SITE_TABLE_FULL parse error");
        else
            CHECK(err == 0, "site-table: 500 func sites parse cleanly");
    }
}

int run_r0_s1_escape_law_tests(void) {
    printf("R0-S1 escape-time binding law: acceptance corpus\n");
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) run_case(&CASES[i]);
    test_site_capacity();
    if (failures == 0) printf("all escape-law corpus checks passed\n");
    return failures;
}
