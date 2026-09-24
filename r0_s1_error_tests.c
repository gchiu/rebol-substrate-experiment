/* r0_s1_error_tests.c - SIN!: a first-class error value, RAISE propagation,
 * and the JUDGE boundary.
 *
 * SIN! is a VALUE: create-sin builds one; holding, passing, storing or
 * comparing it never raises. RAISE is CONTROL FLOW: it unwinds through unaware
 * callers to the nearest JUDGE in the same task, which returns the SIN! as an
 * ordinary value. With no judge the run halts exactly like the old fail-stop,
 * and r0_s1_uncaught_error() reports the error.
 *
 * The escape-time binding law is the first runtime check converted: a
 * violation raises SIN! [type 'escape, id 'return / 'argument / 'store /
 * 'capture / 'transport, arg = the offending block's site id]. It is detected
 * at the same boundary as before, the illegal operation is abandoned, and a
 * judge never receives the illegal value.
 *
 * Each case runs in a fresh runtime (bootstrap + CASE; the M1 task words where
 * a case needs tasks). */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

extern const char *M1_LIB;          /* spawn / run-tasks (r0_s1_m1_tests.c) */

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
    if (err) { N = -1; return -1; }
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

/* symbol id of `name`, as the parser interns it */
static cell sym(const char *name) {
    char buf[64];
    snprintf(buf, sizeof buf, "[ %s ]", name);
    int err = 0;
    cell save = M[GC_LOADER_HP];
    cell b = r0_s1_parse(buf, &err);
    cell w = M[(b & ~(cell)15) + BLK_DATA];
    M[GC_LOADER_HP] = save;
    return word_id(w);
}

/* run prog in a fresh runtime; 1 iff it ends cleanly with the single integer v */
static int value_is(int tasks, const char *prog, long v) {
    if (fresh(tasks) != 0) return 0;
    snprintf(src, sizeof src, "%s", prog);
    run_src();
    return r0_s1_ran_cleanly() && N == 1 && (r0_s1_result(0, N) & 15) == T_INT
           && int_val(r0_s1_result(0, N)) == v;
}

/* run prog in a fresh runtime; 1 iff it halts on an uncaught SIN! with the
 * given type and id (id NULL: not checked) */
static int uncaught_is(int tasks, const char *prog, const char *type, const char *id) {
    if (fresh(tasks) != 0) return 0;
    snprintf(src, sizeof src, "%s", prog);
    run_src();
    cell t, i, a;
    if (!r0_s1_uncaught_error(&t, &i, &a)) return 0;
    if (word_id(t) != sym(type)) return 0;
    if (id && word_id(i) != sym(id)) return 0;
    return 1;
}

/* the whole task table, for "unchanged" comparisons */
static cell table_snap[M1_MAX_TASKS * M1_TASK_REC_SIZE];
static void snap_table(void) {
    memcpy(table_snap, &M[M1_TASK_TABLE], sizeof table_snap);
}
static int table_unchanged(void) {
    return memcmp(table_snap, &M[M1_TASK_TABLE], sizeof table_snap) == 0;
}

/* bootstrap + CASE + one shipped demo, then `reset-tasks`; the table is then
 * snapshotted. Each demo defines its own mnew-task / spawn / scheduler. */
static int fresh_demo(const char *path) {
    if (fresh(0) != 0) return -1;
    if (load_file(path) != 0) return -1;
    snprintf(src, sizeof src, "[ reset-tasks ]");
    if (run_src() != 0) return -1;
    snap_table();
    return 0;
}

/* Escape law (5) through a shipped demo's own spawn/mnew-task: a body bound to
 * go's activation is rejected at transport. `run` is the demo's scheduler call.
 * The body's first statement `res: 1` would be visible if it ever began. */
static void shipped_transport(const char *path, const char *run, const char *tag) {
    char msg[200];
    const char *go = "res: 0  go: func [x] [ spawn [ res: 1  res: x ] ]";

    /* a: unjudged -> uncaught 'escape 'transport, no task created */
    if (fresh_demo(path) == 0) {
        snprintf(src, sizeof src, "[ %s  go 5  %s  res ]", go, run);
        run_src();
        cell t, i;
        int unc = r0_s1_uncaught_error(&t, &i, NULL);
        snprintf(msg, sizeof msg, "%s1: %s spawn of a dependent body halts as SIN! 'escape 'transport", tag, path);
        CHECK(!r0_s1_ran_cleanly() && unc && word_id(t) == sym("escape") && word_id(i) == sym("transport"), msg);
        snprintf(msg, sizeof msg, "%s2: %s rejected spawn leaves the task table byte-identical", tag, path);
        CHECK(table_unchanged(), msg);
    } else {
        snprintf(msg, sizeof msg, "%s1/2: %s setup", tag, path); CHECK(0, msg);
    }

    /* b: judged -> caught, table unchanged, body never began even after the
     * scheduler runs; the error carries the site id (an integer), not the block */
    if (fresh_demo(path) == 0) {
        snprintf(src, sizeof src,
                 "[ %s  e: judge [ go 5 ]  %s"
                 "  either sin? e [ either = sin-type e 'escape"
                 "    [ either = sin-id e 'transport [ res ] [ -3 ] ] [ -2 ] ] [ -1 ] ]", go, run);
        run_src();
        snprintf(msg, sizeof msg, "%s3: %s judge catches 'escape 'transport; body never began (res 0)", tag, path);
        CHECK(r0_s1_ran_cleanly() && N == 1 && r0_s1_result(0, N) == mk_int(0), msg);
        snprintf(msg, sizeof msg, "%s4: %s judged rejection leaves the task table byte-identical", tag, path);
        CHECK(table_unchanged(), msg);
        snprintf(msg, sizeof msg, "%s5: %s no leaked frame after the judged rejection (RV_FRAME 0)", tag, path);
        CHECK(M[RV_FRAME] == 0, msg);
        snprintf(src, sizeof src, "[ sin-arg e ]");
        run_src();
        cell r = (N == 1) ? r0_s1_result(0, N) : 0;
        snprintf(msg, sizeof msg, "%s6: %s the error carries the site id (integer), not the illegal block", tag, path);
        CHECK(r0_s1_ran_cleanly() && N == 1 && (r & 15) == T_INT && int_val(r) > 0, msg);
    } else {
        snprintf(msg, sizeof msg, "%s3-6: %s setup", tag, path); CHECK(0, msg);
    }

    /* c: no slot leak -- 12 judged rejections (4x the slot count), then a full
     * table of legal spawns (one from inside a func) all get slots and run */
    if (fresh_demo(path) == 0) {
        snprintf(src, sizeof src,
                 "[ %s  rep: func [n] [ either > n 0 [ judge [ go 5 ]  rep - n 1 ] [ none ] ]  rep 12"
                 "  good: func [k] [ spawn [ res: + res 100 ] ]"
                 "  h1: spawn [ res: + res 1 ]  h2: good 0  h3: spawn [ res: + res 10 ]"
                 "  %s  %s  %s  res ]", go, run, run, run);
        run_src();
        snprintf(msg, sizeof msg, "%s7: %s after 12 judged rejections, 3 legal spawns get slots and run (res 111)", tag, path);
        CHECK(r0_s1_ran_cleanly() && N == 1 && r0_s1_result(0, N) == mk_int(111), msg);
    } else {
        snprintf(msg, sizeof msg, "%s7: %s setup", tag, path); CHECK(0, msg);
    }
}

int run_r0_s1_error_tests(void) {
    printf("R0-S1 SIN!: first-class error value, RAISE, JUDGE\n");

    /* ---- the value is inert data ------------------------------------------ */
    CHECK(value_is(0, "[ e: create-sin 'user 'boom 42  sin? e ]", 1),
          "E1: create-sin builds a SIN! value (sin? -> 1)");
    CHECK(value_is(0, "[ e: create-sin 'user 'boom 42  f: func [x] [ y: x  y ]  g: f e  = g e ]", 1),
          "E2: a SIN! can be passed, returned, stored and compared without raising");
    CHECK(value_is(0, "[ + + * 100 sin? create-sin 'user 'b 0  * 10 sin? 5  sin? [1] ]", 100),
          "E3: sin? distinguishes SIN! from integers and blocks");
    CHECK(value_is(0, "[ e: create-sin 'user 'boom 42"
                      "  either = sin-type e 'user [ either = sin-id e 'boom [ sin-arg e ] [ -1 ] ] [ -2 ] ]", 42),
          "E4: sin-type / sin-id / sin-arg read the fields");

    /* ---- raise and judge ---------------------------------------------------- */
    CHECK(uncaught_is(0, "[ raise create-sin 'user 'boom 7  99 ]", "user", "boom"),
          "R1: an unjudged raise halts (not a clean run) and reports the error");
    CHECK(value_is(0, "[ e: judge [ raise create-sin 'user 'boom 7  99 ]"
                      "  either sin? e [ sin-arg e ] [ -1 ] ]", 7),
          "R2: judge catches a raised SIN! and returns it as a value");
    CHECK(value_is(0, "[ judge [ 1 2 + 40 2 ] ]", 42),
          "R3: a judge whose block succeeds returns the block's ordinary result");
    CHECK(value_is(0, "[ x: 1  e: judge [ raise create-sin 'user 'boom 0 ]  x: + x 1  x ]", 2),
          "R4: execution continues normally after a judged error (x = 2)");
    CHECK(value_is(0, "[ f: func [n] [ raise create-sin 'user 'deep n ]  e: judge [ f 5 ]  sin-arg e ]", 5),
          "R5: an error crosses one unaware function to the judge");
    CHECK(value_is(0, "[ c: func [n] [ raise create-sin 'user 'deep n  0 ]  b: func [n] [ + 1 c n ]"
                      "  a: func [n] [ + 1 b n ]  e: judge [ a 9 ]  either sin? e [ sin-arg e ] [ -1 ] ]", 9),
          "R6: an error crosses several unaware functions (their pending + never run)");
    CHECK(value_is(0, "[ inner: judge [ raise create-sin 'user 'in 1 ]"
                      "  outer: judge [ judge [ raise create-sin 'user 'x 2 ]  raise create-sin 'user 'y 3 ]"
                      "  + * 10 sin-arg inner sin-arg outer ]", 13),
          "R7: nested judges: each raise is caught by the nearest judge (1 and 3 -> 13)");
    CHECK(value_is(0, "[ f: func [x] [ judge [ + x 1 ] ]  f 41 ]", 42),
          "R8: a judge body inside a function reads its parameters (42)");
    CHECK(value_is(0, "[ f: func [x] [ e: judge [ raise create-sin 'user 'b x ]  + x sin-arg e ]  f 20 ]", 40),
          "R9: after a judged error the function continues with its own state intact (40)");
    CHECK(value_is(0, "[ e: judge [ raise 42 ]  either = sin-type e 'type [ sin-arg e ] [ -1 ] ]", 42),
          "R10: raising a non-SIN! raises a 'type error carrying the value");
    CHECK(value_is(0, "[ e: judge [ sin-arg 5 ]  either sin? e [ = sin-id e 'sin-field ] [ 0 ] ]", 1),
          "R11: a SIN! accessor on a non-error raises a 'type error that judge catches");
    CHECK(uncaught_is(0, "[ judge 5 ]", "type", "judge"),
          "R12: judge on a non-block raises 'type 'judge (uncaught here -> halt)");

    /* ---- RETURN is unchanged, including through a judge --------------------- */
    CHECK(value_is(0, "[ f: func [x] [ either > x 0 [ return 1 ] [ 0 ]  2 ]  f 5 ]", 1),
          "T1: RETURN outside any judge is unchanged");
    CHECK(value_is(0, "[ f: func [x] [ judge [ return 5 ]  7 ]  f 0 ]", 5),
          "T2: RETURN inside a judge body returns from the enclosing function");
    CHECK(value_is(0, "[ g: func [] [ 3 ]  f: func [x] [ judge [ g  return x ]  0 ]  f 8 ]", 8),
          "T3: RETURN after a call inside a judge body still exits the function (8)");
    CHECK(value_is(0, "[ f: func [x] [ case [ [ sin? judge [ raise create-sin 'user 'c 0 ] ] [ + x 1 ] ] ]  f 4 ]", 5),
          "T4: judge works inside a CASE condition; the action still sees x (5)");

    /* ---- GC: error values are traced; landing-pad allocation is safe ------- */
    CHECK(value_is(0, "[ gcs: masm [ LIT 25040 @ LIT 16 MUL ARITY 1 EXIT ]"
                      "  keep: create-sin 'user 'keep 77"
                      "  churn: func [n] [ either > n 0 [ e: judge [ sin-arg 5 ]  churn - n 1 ] [ 0 ] ]"
                      "  rep: func [k] [ either > k 0 [ churn 20  rep - k 1 ] [ 0 ] ]"
                      "  g0: gcs  rep 30"
                      "  + * 1000 sin-arg keep either > gcs g0 [ 1 ] [ 0 ] ]", 77001),
          "G1: 600 judged runtime errors force GCs; a kept SIN! survives intact");

    /* ---- tasks: judges are per task ------------------------------------------ */
    CHECK(value_is(1, "[ res: 0  spawn [ res: sin? judge [ raise create-sin 'user 'task 1 ] ]"
                      "  run-tasks  res ]", 1),
          "K1: a judge inside a task catches that task's error");
    CHECK(uncaught_is(1, "[ spawn [ raise create-sin 'user 'task 1 ]  run-tasks  5 ]", "user", "task"),
          "K2: an unjudged error inside a task halts the run");
    CHECK(uncaught_is(1, "[ e: judge [ spawn [ raise create-sin 'user 'task 1 ]  run-tasks ]  5 ]", "user", "task"),
          "K3: a main-world judge does not catch an error raised inside a task");

    /* ---- the escape law, now raising SIN! 'escape ------------------------- */
    CHECK(uncaught_is(0, "[ f: func [x] [ do [ [x] ] ]  f 7 ]", "escape", "return"),
          "X1: unjudged escape at frame exit still halts, now as SIN! 'escape 'return");
    CHECK(value_is(0, "[ f: func [x] [ do [ [x] ] ]  e: judge [ f 7 ]"
                      "  either = sin-type e 'escape [ = sin-id e 'return ] [ 0 ] ]", 1),
          "X2: frame-exit escape is caught by judge as 'escape 'return");
    if (fresh(0) == 0) {
        snprintf(src, sizeof src, "[ f: func [x] [ do [ [x] ] ]  e: judge [ f 7 ]  sin-arg e ]");
        run_src();
        cell r = (N == 1) ? r0_s1_result(0, N) : 0;
        CHECK(r0_s1_ran_cleanly() && N == 1 && (r & 15) == T_INT && int_val(r) > 0,
              "X3: the judged escape error carries the block's site id (an integer), not the block");
    } else CHECK(0, "X3: setup");
    CHECK(value_is(0, "[ gb: 0  f: func [x] [ gb: [x]  0 ]  e: judge [ f 5 ]"
                      "  either = sin-id e 'store [ gb ] [ -1 ] ]", 0),
          "X4: global-store escape raises 'store and the store never happens (gb still 0)");
    CHECK(value_is(0, "[ f: func [x cl] [ either = cl none [ f 99 [ [1] [ * x 2 ] ] ] [ case cl ] ]"
                      "  e: judge [ f 10 none ]  = sin-id e 'argument ]", 1),
          "X5: same-site argument escape (P30) raises 'argument at the bind");
    CHECK(value_is(0, "[ keeper: func [c] [ func [] [ c ] ]  f: func [x] [ keeper [ [1] [x] ] ]"
                      "  e: judge [ f 5 ]  = sin-id e 'capture ]", 1),
          "X6: foreign-context capture escape (K2) raises 'capture");
    /* N2 (escape corpus): a bound task body must be rejected AT TASK TRANSPORT
     * (mnew-task's ESC_ANY check, before any task-table mutation), so the body
     * never starts. Its first statement `res: 1` would be visible if it ran; a
     * LOAD-LEX guard stop inside a running task would halt with NO uncaught
     * SIN!, so the 'escape 'transport report proves which check fired. */
    if (fresh(1) == 0) {
        snprintf(src, sizeof src, "[ res: 0  go: func [x] [ spawn [ res: 1  res: x ] ]  go 5  run-tasks  res ]");
        run_src();
        cell t, i;
        int unc = r0_s1_uncaught_error(&t, &i, NULL);
        int empty = 1;
        for (int k = 0; k < M1_MAX_TASKS; k++)
            if (M[M1_TASK_TABLE + k * M1_TASK_REC_SIZE + TREC_STATE] != TASK_EMPTY) empty = 0;
        CHECK(!r0_s1_ran_cleanly() && unc && word_id(t) == sym("escape") && word_id(i) == sym("transport"),
              "X7a: N2 is rejected at task transport as SIN! 'escape 'transport (not by LOAD-LEX)");
        CHECK(empty, "X7b: N2's rejected spawn leaves the task table untouched (no task created)");
    } else { CHECK(0, "X7a: setup"); CHECK(0, "X7b: setup"); }
    if (fresh(1) == 0) {
        snprintf(src, sizeof src,
                 "[ res: 0  go: func [x] [ spawn [ res: 1  res: x ] ]  e: judge [ go 5 ]  run-tasks"
                 "  either = sin-id e 'transport [ res ] [ -1 ] ]");
        run_src();
        int empty = 1;
        for (int k = 0; k < M1_MAX_TASKS; k++)
            if (M[M1_TASK_TABLE + k * M1_TASK_REC_SIZE + TREC_STATE] != TASK_EMPTY) empty = 0;
        CHECK(r0_s1_ran_cleanly() && N == 1 && r0_s1_result(0, N) == mk_int(0) && empty,
              "X7c: judged N2: 'transport caught, no task created, body never ran (res still 0)");
    } else CHECK(0, "X7c: setup");
    CHECK(value_is(0, "[ f: func [x] [ case [ [1] [ * x 2 ] ] ]  e: judge [ f 21 ]  e ]", 42),
          "X8: legal downward CASE inside a judge is unaffected (42)");

    /* ---- the SHIPPED task words (not the test library) ---------------------- */
    shipped_transport("demo/shop/demos/tuple-space.glon", "run-tasks", "S");
    shipped_transport("demo/shop/demos/merchant-flow.glon", "run-tasks", "M");
    shipped_transport("demo/shop/demos/linda.glon", "sched-budget run-steps", "L");

    if (failures == 0) printf("all SIN! tests passed\n");
    return failures;
}
