#define _POSIX_C_SOURCE 200809L
/* tests/core-c3/task_tests.c - GLON-C3-2 task runtime regression
 * (make task-runtime-test).
 *
 *   task_tests_<profile> ROOT          all checks (exit 0: all passed)
 *   task_tests_<profile> ROOT negctl   only the response-root check, printing
 *                                      RESP_SURVIVED / RESP_LOST (for the
 *                                      -DR0S1_NO_TASK_RESP_ROOT control build)
 *
 * A session loads what the CLI loads for MODULES=task (prelude, binding,
 * strings, strings-ext, then modules/task.glon) and a `collect` word that calls
 * the collector, so collections happen at exact points.  Covered:
 *
 *   A  scheduling and completion (the M2 examples, sized to the profile's
 *      three task slots): completion, round-robin fairness, failure
 *      isolation, cancellation, many switches, capacity
 *   B  slot reuse and stale handles (the M4 examples): a recycled slot's old
 *      handle is stale for state/wake/cancel/resp-get-for; invalid handles
 *      (negative, out-of-range slot, generation 0, non-integer) write nothing
 *      in or around the task table
 *   C  TREC_RESP lifetime and forced GC: a response held only by TREC_RESP
 *      survives repeated collections and heap reuse while its task is
 *      RUNNABLE (also while blocked); it is cleared at finish and cancel; a
 *      recycled slot starts with none; outside a task set/get touch nothing;
 *      task-held managed values (func locals on task stacks/frames) survive
 *      collections between yields
 *   D  stack exhaustion (GLON-C3-2A): recursion immediately below and beyond
 *      the task-stack limit, at every RP phase, nested block evaluation and
 *      mixed call/block/repeat recursion, pending-argument (data-stack)
 *      pressure; each a clean task failure (SIN! machine stack-exhausted)
 *      with nothing outside the task's slot written; another task runs
 *      afterwards; the slot is reused; the main world halts cleanly
 *      (R0S1_HALT_RS_EXHAUSTED) at every phase
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(c, m) do { checks++; if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)
static const char *root = ".";
static r0_s1_outcome oc;

static int run_src(const char *src, const char *what) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) r0_s1_session_parse_error(&oc);
    else r0_s1_session_run_block(prog, &oc);
    if (oc.status != R0S1_OUT_OK) {
        printf("  (%s: %s / %s)\n", what, r0_s1_outcome_status_name(oc.status),
               r0_s1_outcome_detail_name(oc.detail));
        return -1;
    }
    return 0;
}

static int load_file(const char *rel) {
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return -1; }
    static char buf[200000];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char *r = buf, *w = buf;     /* strip ;; comments outside strings, as the CLI does */
    int in_str = 0;
    while (*r) {
        if (*r == '"') in_str = !in_str;
        if (!in_str && r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    return run_src(buf, rel);
}

/* A fresh session, set up as glon.c's main does for MODULES=task. */
static int fresh(void) {
    cell entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    const char *files[] = { "jupyter/prelude.glon", "demo/shop/binding.glon", "demo/shop/strings.glon",
                            "demo/shop/strings-ext.glon", "modules/task.glon" };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++)
        if (load_file(files[i]) != 0) return -1;
    char src[256];
    snprintf(src, sizeof src, "[ collect: raw [ CALL %ld ARITY 0 EXIT ] ]", (long)r0_s1_gc_collect_addr());
    return run_src(src, "collect");
}

/* the integer value of EXPR (LONG_MIN if it is not an integer or failed) */
static long geti(const char *expr) {
    char src[4096];
    snprintf(src, sizeof src, "[ %s ]", expr);
    if (run_src(src, expr) != 0 || oc.count != 1) return LONG_MIN;
    cell v = r0_s1_result(0, oc.count);
    return r0_tag(v) == T_INT ? (long)int_val(v) : LONG_MIN;
}
static int is_none(const char *expr) {
    char src[4096];
    snprintf(src, sizeof src, "[ %s ]", expr);
    if (run_src(src, expr) != 0 || oc.count != 1) return 0;
    return r0_s1_result(0, oc.count) == R0_NONE;
}
static cell rec(int slot) { return M1_TASK_TABLE + (cell)slot * M1_TASK_REC_SIZE; }
static int all_resp_none(void) {
    for (int i = 0; i < M1_MAX_TASKS; i++) if (M[rec(i) + TREC_RESP] != R0_NONE) return 0;
    return 1;
}

/* ---------------------------------------------------------------- A ---- */
static void test_scheduling(void) {
    printf("A: scheduling and completion (%d task slots)\n", (int)M1_MAX_TASKS);
    if (fresh()) { CHECK(0, "A session"); return; }
    run_src("[ count: 0 "
            "  spawn [ repeat 3 [ count: + count 1  yield ] ] "
            "  spawn [ repeat 3 [ count: + count 1  yield ] ] "
            "  spawn [ repeat 3 [ count: + count 1  yield ] ] "
            "  run-tasks ]", "A1");
    CHECK(geti("count") == 9, "A1 three tasks x three yields complete (count 9)");
    CHECK(geti("tasks/pending?") == 0, "A1 nothing pending afterwards");
    int fin = 1;
    for (int i = 0; i < M1_MAX_TASKS; i++) if (M[rec(i) + TREC_STATE] != TASK_FINISHED) fin = 0;
    CHECK(fin, "A1 every slot FINISHED");

    if (fresh()) { CHECK(0, "A2 session"); return; }
    run_src("[ trace: 0 "
            "  t1: spawn [ repeat 3 [ trace: + * trace 10 1  yield ] ] "
            "  t2: spawn [ repeat 3 [ trace: + * trace 10 2  yield ] ] "
            "  t3: spawn [ repeat 3 [ trace: + * trace 10 3  yield ] ] "
            "  run-tasks ]", "A2");
    CHECK(geti("trace") == 123123123L, "A2 round-robin fairness (trace 123123123)");

    if (fresh()) { CHECK(0, "A3 session"); return; }
    run_src("[ captured: none  peer: 0 "
            "  t1: spawn [ raise create-sin 'app 'boom 0 ] "
            "  t2: spawn [ captured: tasks/error  peer: 1 ] "
            "  run-tasks ]", "A3");
    CHECK(geti("either sin? captured [ 1 ] [ 0 ]") == 1 && geti("peer") == 1,
          "A3 a raising task is isolated; the peer runs and sees the SIN!");
    CHECK(geti("tasks/pending?") == 0, "A3 the scheduler finished both");

    if (fresh()) { CHECK(0, "A4 session"); return; }
    run_src("[ bad: 0  ran: 0 "
            "  t1: spawn [ bad: 1 ] "
            "  task-cancel t1 "
            "  st: task-state t1 "
            "  t2: spawn [ ran: 1 ] "
            "  run-tasks ]", "A4");
    CHECK(geti("st") == 1 && geti("bad") == 0 && geti("ran") == 1,
          "A4 a cancelled task never runs (state 1); its peer runs");

    if (fresh()) { CHECK(0, "A5 session"); return; }
    /* `repeat` recurses once per iteration and a task's return stack holds
     * about 26 levels (section D), so 200 iterations are nested 10 x 20 deep
     * rather than 200 deep (200 deep is now a clean task failure, D2) */
    run_src("[ switches: 0 "
            "  bump: func [] [ switches: + switches 1 ] "
            "  spawn [ repeat 10 [ repeat 10 [ repeat 2 [ bump  yield ] ] ] ] "
            "  spawn [ repeat 10 [ repeat 10 [ repeat 2 [ bump  yield ] ] ] ] "
            "  spawn [ repeat 10 [ repeat 10 [ repeat 2 [ bump  yield ] ] ] ] "
            "  run-tasks ]", "A5");
    CHECK(geti("switches") == 600, "A5 600 cooperative switches");

    if (fresh()) { CHECK(0, "A6 session"); return; }
    run_src("[ a: spawn [ task-wait ]  b: spawn [ task-wait ]  c: spawn [ task-wait ] "
            "  d: spawn [ none ]  run-tasks ]", "A6");
    CHECK(geti("d") == -1, "A6 a spawn beyond the active slots answers -1");
    CHECK(geti("tasks/pending?") == 1, "A6 three blocked tasks are pending");
    run_src("[ task-cancel a  task-cancel b  task-cancel c ]", "A6 cancel");
    CHECK(geti("tasks/pending?") == 0, "A6 cancelling them leaves nothing pending");
}

/* ---------------------------------------------------------------- B ---- */
static void test_reuse_and_handles(void) {
    printf("B: slot reuse, generations and invalid handles\n");
    if (fresh()) { CHECK(0, "B1 session"); return; }
    run_src("[ made: 0  limit: 50 "
            "  driver: func [] [ made: + made 1  either < made limit [ spawn [ driver ] ] [ none ] ] "
            "  spawn [ driver ]  run-tasks ]", "B1");
    CHECK(geti("made") == 50, "B1 a 50-task chain completes through recycled slots");

    if (fresh()) { CHECK(0, "B2 session"); return; }
    run_src("[ made: 0  limit: 10000 "
            "  driver: func [] [ made: + made 1  either < made limit [ spawn [ driver ] ] [ none ] ] "
            "  spawn [ driver ]  run-tasks ]", "B2");
    long gens = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) gens += (long)M[rec(i) + TREC_GEN];
    CHECK(geti("made") == 10000, "B2 10,000 task creations (endurance)");
    CHECK(gens == 10000, "B2 one generation bump per creation (sum of TREC_GEN = 10,000)");
    CHECK(all_resp_none(), "B2 no slot holds a response afterwards");

    if (fresh()) { CHECK(0, "B3 session"); return; }
    run_src("[ woken: 0 "
            "  t1: spawn [ none ]  run-tasks  old: t1 "
            "  t2: spawn [ task-wait  woken: 1 ]  run-tasks "
            "  task-wake old  run-tasks ]", "B3");
    CHECK(geti("woken") == 0, "B3 a stale handle cannot wake the task that recycled its slot");
    CHECK(geti("task-state old") == -1, "B3 task-state of the stale handle is -1");
    CHECK(geti("task-state t2") == 0, "B3 the new occupant is still runnable (blocked)");
    run_src("[ task-cancel old ]", "B3 cancel old");
    CHECK(geti("task-state t2") == 0, "B3 task-cancel with the stale handle leaves the new occupant");
    CHECK(is_none("task-resp-get-for old"), "B3 task-resp-get-for of the stale handle is none");
    run_src("[ task-cancel t2 ]", "B3 cancel t2");
    CHECK(geti("tasks/pending?") == 0, "B3 the current handle cancels it");

    /* invalid handles: nothing in or around the task table may change */
    if (fresh()) { CHECK(0, "B4 session"); return; }
    run_src("[ t1: spawn [ task-wait ]  run-tasks ]", "B4 setup");   /* slot 0 live, gen 1 */
    enum { GUARD = 64 };
    cell lo = M1_TASK_TABLE - GUARD, hi = rec(M1_MAX_TASKS) + GUARD;
    static cell snap[4096];
    if (hi - lo > (cell)(sizeof snap / sizeof snap[0])) { CHECK(0, "B4 snapshot size"); return; }
    for (cell a = lo; a < hi; a++) snap[a - lo] = M[a];
    long s1 = geti("task-state -1"), s2 = geti("task-state 80"), s3 = geti("task-state -4096");
    long s4 = geti("task-state 0"), s5 = geti("task-state [ 1 ]"), s6 = geti("task-state \"h\"");
    long s7 = geti("task-state 1048576");
    run_src("[ task-wake -1  task-wake 80  task-wake 0  task-wake [ 1 ]  task-wake 1048576 "
            "  task-cancel -1  task-cancel 80  task-cancel 0  task-cancel [ 1 ]  task-cancel \"h\" "
            "  task-cancel 1048576  task-cancel -16 ]", "B4 writers");
    int n1 = is_none("task-resp-get-for -1"), n2 = is_none("task-resp-get-for 80");
    int n3 = is_none("task-resp-get-for 0"), n4 = is_none("task-resp-get-for [ 1 ]");
    int changed = 0;
    for (cell a = lo; a < hi; a++) if (M[a] != snap[a - lo]) { changed++; if (changed <= 4) printf("    changed M[%ld]\n", (long)a); }
    CHECK(s1 == -1 && s2 == -1 && s3 == -1 && s4 == -1 && s5 == -1 && s6 == -1 && s7 == -1,
          "B4 task-state of -1, slot 5 (80), -4096, generation 0, a block, a string, a foreign generation: -1");
    CHECK(n1 && n2 && n3 && n4, "B4 task-resp-get-for of invalid handles: none");
    CHECK(changed == 0, "B4 invalid handles to wake/cancel write nothing in the task table or 64 cells around it");
    CHECK(geti("task-state t1") == 0, "B4 the live task is untouched");
    run_src("[ task-cancel t1 ]", "B4 cleanup");
}

/* ---------------------------------------------------------------- C ---- */
/* The response is built inside task A and held only by TREC_RESP (no global,
 * no local).  Task B collects, allocates same-sized strings that would reuse
 * a freed block, collects again, reads A's response through A's handle and
 * wakes A; A reads its own response.  Returns 1 if both reads matched. */
static int resp_survives(long *gc_before, long *gc_after) {
    if (fresh()) return -1;
    *gc_before = r0_s1_gc_count();
    run_src("[ seen-b: 0  seen-a: 0 "
            "  ha: spawn [ task-resp-set s/+ \"resp-\" s/from-int 12345 "
            "              task-wait "
            "              seen-a: either str-eq task-resp-get \"resp-12345\" [ 1 ] [ 0 ] ] "
            "  hb: spawn [ collect "
            "              repeat 5 [ repeat 5 [ repeat 5 [ repeat 4 [ "
            "                  junk: s/+ \"resp-\" s/from-int 99999  junk: s/+ \"resp-\" s/from-int 99998 "
            "                  junk: s/+ \"resp-\" s/from-int 99997 ] ] ] ] "
            "              collect  collect "
            "              r: task-resp-get-for ha "
            "              seen-b: either str-eq r \"resp-12345\" [ 1 ] [ 0 ] "
            "              r: none "
            "              task-wake ha ] "
            "  run-tasks ]", "C1");
    *gc_after = r0_s1_gc_count();
    return geti("seen-a") == 1 && geti("seen-b") == 1;
}

#define J "junk: s/+ \"gamma-\" s/from-int 333 "
static void test_resp(void) {
    printf("C: TREC_RESP lifetime and forced collection\n");
    long g0, g1;
    int ok = resp_survives(&g0, &g1);
    CHECK(g1 - g0 >= 3, "C1 the collector ran during the test (forced collections)");
    CHECK(ok == 1, "C1 a response held only by TREC_RESP survives collections and heap reuse while its task is blocked");
    CHECK(all_resp_none(), "C1 the response is cleared when its task finishes");
    CHECK(is_none("task-resp-get-for ha"), "C1 task-resp-get-for after the task finished: none");

    if (fresh()) { CHECK(0, "C2 session"); return; }
    run_src("[ t1: spawn [ task-resp-set s/+ \"old-\" s/from-int 1 ]  run-tasks ]", "C2 first life");
    CHECK(all_resp_none(), "C2 finish clears the response");
    run_src("[ inherited: 1  t2: spawn [ inherited: either = task-resp-get none [ 0 ] [ 1 ] ]  run-tasks ]",
            "C2 second life");
    CHECK(geti("inherited") == 0, "C2 a task in a recycled slot starts with no response");

    if (fresh()) { CHECK(0, "C3 session"); return; }
    run_src("[ t1: spawn [ task-resp-set s/+ \"cancelled-\" s/from-int 2  task-wait ]  run-tasks ]", "C3");
    int set_while_blocked = M[rec(0) + TREC_RESP] != R0_NONE;
    run_src("[ task-cancel t1 ]", "C3 cancel");
    CHECK(set_while_blocked && all_resp_none() && M[rec(0) + TREC_STATE] == TASK_FINISHED,
          "C3 cancel clears a blocked task's response");

    if (fresh()) { CHECK(0, "C4 session"); return; }
    M[M1_CUR_TASK] = 0;                   /* as at CLI start: no task has run */
    cell low13 = M[13];
    run_src("[ task-resp-set \"top-level\" ]", "C4 set outside");
    CHECK(all_resp_none() && M[13] == low13, "C4 task-resp-set outside a task writes nothing (M1_CUR_TASK 0)");
    CHECK(is_none("task-resp-get"), "C4 task-resp-get outside a task: none");
    run_src("[ t1: spawn [ none ]  run-tasks  task-resp-set \"after\" ]", "C4 after run-tasks");
    CHECK(M[M1_CUR_TASK] == rec(0) && all_resp_none(),
          "C4 after run-tasks returned (M1_CUR_TASK still names slot 0), set writes nothing");
    CHECK(is_none("task-resp-get"), "C4 task-resp-get after run-tasks: none");

    if (fresh()) { CHECK(0, "C5 session"); return; }
    long c0 = r0_s1_gc_count();
    run_src("[ ok-a: 0  ok-b: 0 "
            "  hold: func [tag n] [ v: s/+ tag s/from-int n  repeat 4 [ yield ]  str-eq v s/+ tag s/from-int n ] "
            "  spawn [ ok-a: either hold \"alpha-\" 111 [ 1 ] [ 0 ] ] "
            "  spawn [ ok-b: either hold \"beta-\" 222 [ 1 ] [ 0 ] ] "
            "  spawn [ repeat 4 [ collect  repeat 5 [ repeat 6 [ " J J J J J J J J J J " ] ]  yield ] ] "
            "  run-tasks ]", "C5");
    long c5n = r0_s1_gc_count() - c0;
    printf("    C5 collections: %ld\n", c5n);
    CHECK(c5n >= 4, "C5 collections ran between the tasks' yields");
    CHECK(geti("ok-a") == 1 && geti("ok-b") == 1,
          "C5 managed values in suspended tasks' locals survive collections");
}

/* ---------------------------------------------------------------- D ---- */
/* GLON-C3-2A stack boundaries.  Poison every cell a task in slot 0 must not
 * write -- the 512 cells below its slot (the loader heap's top) and the
 * whole of slots 1..M1_MAX_TASKS-1 -- run, and check the poison. */
static const cell POISON = 0x5A5A5A5L;
static void poison_around_slot0(void) {
    for (cell c = M1_ARENA_BASE - 512; c < M1_ARENA_BASE; c++) M[c] = POISON;
    for (cell c = M1_ARENA_BASE + M1_TASK_CELLS; c < M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS; c++) M[c] = POISON;
}
static int around_slot0_intact(void) {
    for (cell c = M1_ARENA_BASE - 512; c < M1_ARENA_BASE; c++) if (M[c] != POISON) return 0;
    for (cell c = M1_ARENA_BASE + M1_TASK_CELLS; c < M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS; c++)
        if (M[c] != POISON) return 0;
    return 1;
}
/* run `body` as the only task, after `pre`; 1 completed (done = 1), 2 failed
 * with SIN! [machine stack-exhausted reason], 0 anything else (incl. a halt) */
static int task_outcome(const char *pre, const char *body, long *reason) {
    if (fresh()) return -1;
    char src[60000];
    /* one task only (slot 0): its judge leaves the result or SIN! in
     * tasks/error, read at top level after run-tasks */
    snprintf(src, sizeof src, "[ done: 0  err: none  c: 0  %s  spawn [ %s  done: 1 ] "
             "  run-tasks  err: tasks/error ]", pre, body);
    poison_around_slot0();
    if (run_src(src, "D task") != 0) return 0;
    if (geti("done") == 1 && geti("either sin? err [ 1 ] [ 0 ]") == 0) return 1;
    if (geti("either sin? err [ either = sin-id err 'stack-exhausted [ 1 ] [ 0 ] ] [ 0 ]") == 1) {
        *reason = geti("sin-arg err");
        return 2;
    }
    return 0;
}

static void test_stacks(void) {
    printf("D: task-stack exhaustion (C3-2A; R0S1_RS_GUARD %ld, R0S1_DS_GUARD %ld)\n",
           (long)R0S1_RS_GUARD, (long)R0S1_DS_GUARD);
    /* D1: the limit, and immediately below and beyond it */
    int limit = -1, below_ok = 1, beyond_ok = 1, intact = 1;
    long reason = 0;
    for (int n = 5; n <= 60; n++) {
        char body[128];
        snprintf(body, sizeof body, "repeat %d [ c: + c 1 ]", n);
        long r = 0;
        int o = task_outcome("", body, &r);
        if (!around_slot0_intact()) intact = 0;
        if (limit < 0) {
            if (o == 1) { if (geti("c") != n) below_ok = 0; }
            else if (o == 2) { limit = n; reason = r; }
            else { below_ok = 0; limit = n; }
        } else if (o != 2) beyond_ok = 0;
    }
    printf("    task recursion limit: repeat %d fails, %d completes\n", limit, limit - 1);
    CHECK(limit > 5 && below_ok, "D1 recursion below the task-stack limit completes with the right result");
    CHECK(limit > 0 && beyond_ok && reason == R0S1_HALT_RS_EXHAUSTED,
          "D1 recursion at and beyond the limit (to 60) is a task failure: SIN! machine stack-exhausted 5");
    CHECK(intact, "D1 nothing below the slot or in the other slots was written");

    /* D2: the defect's own reproductions: repeat 40 and 200 in a task */
    long r = 0;
    int o40 = task_outcome("", "repeat 40 [ c: + c 1 ]", &r), i40 = around_slot0_intact();
    int o200 = task_outcome("", "repeat 200 [ c: + c 1 ]", &r), i200 = around_slot0_intact();
    CHECK(o40 == 2 && o200 == 2 && i40 && i200,
          "D2 repeat 40 / 200 in a task (formerly 'bad opcode ... at 1') fail cleanly, nothing outside the slot written");

    /* D3: every phase -- 0..24 nested do levels shift where RP falls */
    int phase_bad = 0, phase_runs = 0, phase_intact = 1;
    for (int w = 0; w <= 24; w++)
        for (int n = 20; n <= 40; n += 2) {
            char body[512]; int k = 0;
            for (int i = 0; i < w; i++) k += snprintf(body + k, sizeof body - (size_t)k, "do [ ");
            k += snprintf(body + k, sizeof body - (size_t)k, "repeat %d [ c: + c 1 ] ", n);
            for (int i = 0; i < w; i++) k += snprintf(body + k, sizeof body - (size_t)k, "] ");
            long rr = 0;
            int o = task_outcome("", body, &rr);
            phase_runs++;
            if (o != 1 && o != 2) phase_bad++;
            if (!around_slot0_intact()) phase_intact = 0;
        }
    printf("    phase sweep: %d runs\n", phase_runs);
    CHECK(phase_bad == 0 && phase_intact,
          "D3 at every phase (0-24 nested do, depth 20-40) a task completes or fails cleanly; nothing outside its slot written");

    /* D4: nested block evaluation (no closure invocation) and mixed work */
    char deep[20000]; int k = 0;
    for (int i = 0; i < 800; i++) k += snprintf(deep + k, sizeof deep - (size_t)k, "do [ ");
    k += snprintf(deep + k, sizeof deep - (size_t)k, "c: 7 ");
    for (int i = 0; i < 800; i++) k += snprintf(deep + k, sizeof deep - (size_t)k, "] ");
    long rd = 0;
    int od = task_outcome("", deep, &rd), idd = around_slot0_intact();
    CHECK(od == 2 && rd == R0S1_HALT_RS_EXHAUSTED && idd,
          "D4 800 nested do blocks in a task (formerly unguarded) fail cleanly on the return stack");
    k = 0;
    for (int i = 0; i < 300; i++) k += snprintf(deep + k, sizeof deep - (size_t)k, "either 1 [ ");
    k += snprintf(deep + k, sizeof deep - (size_t)k, "c: 7 ");
    for (int i = 0; i < 300; i++) k += snprintf(deep + k, sizeof deep - (size_t)k, "] [ 0 ] ");
    int oe = task_outcome("", deep, &rd);
    CHECK(oe == 1 && geti("c") == 7, "D4 300 nested either blocks in a task complete");
    int om = task_outcome("f: func [n] [ either > n 0 [ do [ either 1 [ repeat 2 [ c: + c 1 ] ] [ 0 ] ]  f - n 1 ] [ none ] ]",
                          "f 100", &rd), im = around_slot0_intact();
    CHECK(om == 2 && im, "D4 mixed call/block/repeat recursion past the limit fails cleanly");
    int om2 = task_outcome("f: func [n] [ either > n 0 [ do [ either 1 [ repeat 2 [ c: + c 1 ] ] [ 0 ] ]  f - n 1 ] [ none ] ]",
                           "f 6", &rd);
    CHECK(om2 == 1 && geti("c") == 12, "D4 the same mixed workload within the limit completes (c 12)");

    /* D5: data-stack exhaustion: each level leaves 14 pending arguments */
    int ods = task_outcome("g: func [a b c d e f g h i j k l m n o] [ 0 ] "
                           "h: func [n] [ either > n 0 [ g 1 2 3 4 5 6 7 8 9 10 11 12 13 14 h - n 1 ] [ 0 ] ]",
                           "h 40", &rd), ids = around_slot0_intact();
    long ds_reason = rd;
    printf("    pending-argument recursion: reason %ld\n", ds_reason);
    CHECK(ods == 2 && ids && (ds_reason == R0S1_HALT_DS_EXHAUSTED || ds_reason == R0S1_HALT_RS_EXHAUSTED),
          "D5 recursion with 14 pending arguments per level fails cleanly (stack exhausted), nothing outside the slot written");

    /* D6: exhaustion, then another task runs normally */
    if (fresh()) { CHECK(0, "D6 session"); return; }
    run_src("[ seen: none  b-sum: 0 "
            "  a: spawn [ repeat 200 [ none ] ] "
            "  b: spawn [ seen: tasks/error  b-sum: + 40 2 ] "
            "  run-tasks ]", "D6");
    CHECK(geti("b-sum") == 42 && geti("either sin? seen [ either = sin-id seen 'stack-exhausted [ 1 ] [ 0 ] ] [ 0 ]") == 1,
          "D6 after one task exhausts its stack, the next task runs and sees the SIN!");
    CHECK(geti("tasks/pending?") == 0 && geti("task-state a") == 1, "D6 the failed task is finished; nothing pending");

    /* D7: the exhausted task's slot is reused, and the new task's stacks are whole */
    char body[256];
    snprintf(body, sizeof body, "[ c: 0  t: spawn [ repeat %d [ c: + c 1 ] ]  run-tasks ]", limit - 1);
    run_src(body, "D7");
    CHECK(geti("c") == limit - 1, "D7 a task in the exhausted task's slot recurses to just below the limit");
    CHECK(geti("task-state a") == -1, "D7 the exhausted task's handle is stale after reuse");

    /* D8: the main world keeps its fatal fail-stop, now at every phase */
    int main_bad = 0, main_runs = 0;
    for (int w = 0; w <= 24; w++)
        for (int n = 30; n <= 60; n += 3) {
            if (fresh()) { main_bad++; continue; }
            char src[1024]; int kk = snprintf(src, sizeof src, "[ rep: func [n b] [ either > n 0 [ do b rep - n 1 b ] [ none ] ] c: 0 ");
            for (int i = 0; i < w; i++) kk += snprintf(src + kk, sizeof src - (size_t)kk, "do [ ");
            kk += snprintf(src + kk, sizeof src - (size_t)kk, "rep %d [ c: + c 1 ] ", n);
            for (int i = 0; i < w; i++) kk += snprintf(src + kk, sizeof src - (size_t)kk, "] ");
            snprintf(src + kk, sizeof src - (size_t)kk, "]");
            int perr = 0; cell prog = r0_s1_parse(src, &perr);
            r0_s1_session_run_block(prog, &oc);
            main_runs++;
            if (oc.status == R0S1_OUT_OK) continue;
            if (!(oc.status == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_RS_EXHAUSTED)) main_bad++;
        }
    printf("    main-world phase sweep: %d runs\n", main_runs);
    CHECK(main_bad == 0, "D8 main-world recursion completes or halts with R0S1_HALT_RS_EXHAUSTED at every phase");

    /* D9: a collection at every recursion level, so collections start near
     * the floor (the collector-entry guard, R0S1_GC_RS_GUARD), at every phase,
     * in a task and in the main world */
    int gc_bad = 0, gc_runs = 0, gc_intact = 1, gc_fail = 0;
    long gc0 = r0_s1_gc_count();
    for (int w = 0; w <= 16; w++)
        for (int n = 14; n <= 34; n += 2) {
            char pre[1024]; int kk = snprintf(pre, sizeof pre, "rec: func [n] [ either > n 0 [ collect  c: + c 1  ");
            for (int i = 0; i < w; i++) kk += snprintf(pre + kk, sizeof pre - (size_t)kk, "do [ ");
            kk += snprintf(pre + kk, sizeof pre - (size_t)kk, "rec - n 1 ");
            for (int i = 0; i < w; i++) kk += snprintf(pre + kk, sizeof pre - (size_t)kk, "] ");
            snprintf(pre + kk, sizeof pre - (size_t)kk, "] [ 0 ] ]");
            char b2[64]; snprintf(b2, sizeof b2, "rec %d", n);
            long rr = 0;
            int o = task_outcome(pre, b2, &rr);
            gc_runs++;
            if (o == 2) gc_fail++;
            if (o != 1 && o != 2) gc_bad++;
            if (o == 1 && geti("c") != n) gc_bad++;
            if (!around_slot0_intact()) gc_intact = 0;
        }
    printf("    collect-per-level sweep in a task: %d runs, %d clean task failures, %ld collections\n",
           gc_runs, gc_fail, r0_s1_gc_count() - gc0);
    CHECK(gc_bad == 0 && gc_intact && gc_fail > 0,
          "D9 collections near the task-stack floor: complete or a clean task failure, nothing outside the slot written");
    int gm_bad = 0, gm_halt = 0;
    for (int n = 20; n <= 140; n += 3) {          /* past an 8,192-cell main stack too */
        if (fresh()) { gm_bad++; continue; }
        char src[512];
        snprintf(src, sizeof src, "[ c: 0  rec: func [n] [ either > n 0 [ collect  c: + c 1  rec - n 1 ] [ 0 ] ]  rec %d ]", n);
        int perr = 0; cell prog = r0_s1_parse(src, &perr);
        r0_s1_session_run_block(prog, &oc);
        if (oc.status == R0S1_OUT_OK) { if (geti("c") != n) gm_bad++; continue; }
        if (oc.status == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_RS_EXHAUSTED) gm_halt++;
        else gm_bad++;
    }
    printf("    collect-per-level in the main world: %d clean halts, %d other\n", gm_halt, gm_bad);
    CHECK(gm_bad == 0 && gm_halt > 0, "D9 main world: collections near the floor complete or halt with R0S1_HALT_RS_EXHAUSTED");
}

/* ---------------------------------------------------------------- E ---- */
/* GLON-C3-2D: the derived worst case between two evaluation steps (99 cells:
 * native INVOKE -> closure, context padding 15), forced collections at the
 * maximum safe frame depth (the collector's frame-chain walk is iterative),
 * nested vocabularies across collections. */

/* a fresh main-world run of SRC; 1 OK, 2 halted with R0S1_HALT_RS_EXHAUSTED,
 * 0 anything else */
static int main_outcome(const char *src) {
    if (fresh()) return 0;
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) return 0;
    r0_s1_session_run_block(prog, &oc);
    if (oc.status == R0S1_OUT_OK) return 1;
    if (oc.status == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_RS_EXHAUSTED) return 2;
    return 0;
}

/* k nested do wrappers around TEXT (each shifts the return stack by 5 cells;
 * gcd(5, 16) = 1, so k = 0..15 gives every context-alignment padding) */
static void wrap_do(char *out, size_t cap, int k, const char *text) {
    size_t n = 0;
    for (int i = 0; i < k; i++) n += (size_t)snprintf(out + n, cap - n, "do [ ");
    n += (size_t)snprintf(out + n, cap - n, "%s ", text);
    for (int i = 0; i < k; i++) n += (size_t)snprintf(out + n, cap - n, "] ");
}

/* nested vocabularies whose members (closures, a block) must survive every
 * collection; outer/g -> 6 */
#define VOCABS "vocabulary outer [ x: 5  vocabulary inner [ f: func [a] [ + a 1 ]  blk: [ 1 2 3 ] ] " \
               "  g: func [] [ inner/f x ] ] "
/* level n keeps a closure k answering n, reachable only from its activation's
 * context; at the bottom: collect, allocate over freed cells, collect again,
 * then use the vocabularies.  Result: n(n+1)/2 + 13 (6 + 4 + 3) */
#define DEEP_GC "rec: func [n] [ k: func [] [ n ]  either > n 0 [ + rec - n 1  k ] " \
                "[ collect  z1: func [] [ 1 ]  z2: func [] [ 2 ]  z3: [ 1 2 ]  collect " \
                "  + outer/g  + outer/inner/f block-at outer/inner/blk 2  + z1 z2 ] ] "

static void test_worst_case_and_gc(void) {
    printf("E: worst-case step path and collections at maximum depth (C3-2D)\n");
    char text[1024], src[4096], pre[4096];

    /* E1: the 99-cell path (INVOKE of a zero-arity closure) and the 98-cell
     * one (the same closure through a word), at every padding phase, from
     * completion to exhaustion -- in a task (poisoned surroundings) and in
     * the main world */
    const char *call[2] = { "invoke :g", "g" };
    int e1_bad = 0, e1_runs = 0, e1_intact = 1, e1_phases_both = 0;
    for (int v = 0; v < 2; v++)
        for (int k = 0; k < 16; k++) {
            wrap_do(text, sizeof text, k, call[v]);
            snprintf(pre, sizeof pre, "f: func [n] [ g: func [] [ either < n 1 [ c ] [ c: + c 1  f - n 1 ] ]  %s ]", text);
            int seen_ok = 0, seen_fail = 0, failed = 0;
            for (int n = 2; n <= 30; n++) {
                char body[64]; snprintf(body, sizeof body, "c: f %d", n);
                long r = 0;
                int o = task_outcome(pre, body, &r);
                e1_runs++;
                if (!around_slot0_intact()) e1_intact = 0;
                if (o == 1) { seen_ok = 1; if (failed || geti("c") != n) e1_bad++; }
                else if (o == 2 && r == R0S1_HALT_RS_EXHAUSTED) { seen_fail = 1; failed = 1; }
                else e1_bad++;
            }
            if (seen_ok && seen_fail) e1_phases_both++;
        }
    printf("    task: %d runs, %d of 32 variant/phase series both complete and exhaust\n", e1_runs, e1_phases_both);
    CHECK(e1_bad == 0 && e1_intact && e1_phases_both == 32,
          "E1 task: INVOKE (99) and word (98) worst paths at all 16 paddings complete, then fail cleanly; nothing outside the slot written");
    int m_bad = 0, m_both = 0;
    for (int v = 0; v < 2; v++)
        for (int k = 0; k < 16; k++) {
            wrap_do(text, sizeof text, k, call[v]);
            int seen_ok = 0, seen_halt = 0, halted = 0;
            for (int n = 10; n <= 50; n += 2) {
                snprintf(src, sizeof src, "[ c: 0  f: func [n] [ g: func [] [ either < n 1 [ c ] [ c: + c 1  f - n 1 ] ]  %s ]  c: f %d ]",
                         text, n);
                int o = main_outcome(src);
                if (o == 1) { seen_ok = 1; if (halted || geti("c") != n) m_bad++; }
                else if (o == 2) { seen_halt = 1; halted = 1; }
                else m_bad++;
            }
            if (seen_ok && seen_halt) m_both++;
        }
    CHECK(m_bad == 0 && m_both == 32,
          "E1 main: the same worst paths at all 16 paddings complete, then halt with R0S1_HALT_RS_EXHAUSTED (no other halt)");

    /* E2: a collection at the deepest level the main world supports, at
     * every phase: every frame's context (and the closures only it holds)
     * survives, and one level deeper halts cleanly */
    int e2_bad = 0, maxd_lo = 1 << 30, maxd_hi = 0;
    for (int k = 0; k < 16; k++) {
        int lo = 1, hi = 200;                       /* lo completes, hi does not */
        while (hi - lo > 1) {
            int mid = (lo + hi) / 2;
            char call1[32]; snprintf(call1, sizeof call1, "rec %d", mid);
            wrap_do(text, sizeof text, k, call1);
            snprintf(src, sizeof src, "[ " VOCABS DEEP_GC " r: %s ]", text);
            if (main_outcome(src) == 1) lo = mid; else hi = mid;
        }
        char call2[32]; snprintf(call2, sizeof call2, "rec %d", lo);
        wrap_do(text, sizeof text, k, call2);
        snprintf(src, sizeof src, "[ " VOCABS DEEP_GC " r: %s ]", text);
        int o = main_outcome(src);
        if (o != 1 || geti("r") != (long)lo * (lo + 1) / 2 + 13) e2_bad++;
        snprintf(call2, sizeof call2, "rec %d", lo + 1);
        wrap_do(text, sizeof text, k, call2);
        snprintf(src, sizeof src, "[ " VOCABS DEEP_GC " r: %s ]", text);
        if (main_outcome(src) != 2) e2_bad++;
        if (lo < maxd_lo) maxd_lo = lo;
        if (lo > maxd_hi) maxd_hi = lo;
    }
    printf("    main: deepest level with a collection at the bottom: %d-%d (16 phases)\n", maxd_lo, maxd_hi);
    CHECK(e2_bad == 0 && maxd_lo > 5,
          "E2 main: collection at the maximum safe depth keeps every frame's closures and the nested vocabularies; one deeper halts cleanly");

    /* E3: a collection at the deepest level a task supports while the main
     * world is itself deep (main's frame chain is marked on the TASK's
     * stack), at every phase; nothing outside the task's slot is written */
    int mdeep = maxd_lo - 2, e3_bad = 0, e3_intact = 1, tmax_lo = 1 << 30, tmax_hi = 0;
    for (int k = 0; k < 16; k++) {
        int lo = 0, hi = 60;
        int ok_lo = 0;
        while (hi - lo > 1) {
            int mid = (lo + hi) / 2;
            char call3[32]; snprintf(call3, sizeof call3, "rec %d", mid);
            wrap_do(text, sizeof text, k, call3);
            snprintf(src, sizeof src,
                     "[ " VOCABS DEEP_GC " tsum: none "
                     " mrec: func [n] [ k: func [] [ n ]  either > n 0 [ + mrec - n 1  k ] "
                     "   [ spawn [ set 'tsum %s ]  run-tasks  0 ] ] "
                     " msum: mrec %d ]", text, mdeep);
            poison_around_slot0();
            int o = main_outcome(src);
            if (!around_slot0_intact()) e3_intact = 0;
            if (o == 1 && geti("msum") == (long)mdeep * (mdeep + 1) / 2 &&
                geti("tsum") == (long)mid * (mid + 1) / 2 + 13) { lo = mid; ok_lo = 1; }
            else {
                /* past the task's limit: the task fails cleanly (tsum stays
                 * none) and the main world finishes */
                if (!(o == 1 && is_none("tsum") && geti("msum") == (long)mdeep * (mdeep + 1) / 2)) e3_bad++;
                hi = mid;
            }
        }
        if (!ok_lo) e3_bad++;
        if (lo < tmax_lo) tmax_lo = lo;
        if (lo > tmax_hi) tmax_hi = lo;
    }
    printf("    task (main %d deep): deepest level with a collection at the bottom: %d-%d\n", mdeep, tmax_lo, tmax_hi);
    CHECK(e3_bad == 0 && e3_intact && tmax_lo > 3,
          "E3 task with a deep main world: collection at the task's maximum depth keeps both worlds' frames; past it the task fails cleanly");

    /* E4: the suspended main world's locals survive collections inside tasks
     * (GLON-C3-2D roots M1_SCHED_REC+5 / +7; inherited W5.1 defect: they were
     * not marked, so a task's collection freed them).  keep's locals: a
     * closure over its parameter, a closure from a nested activation (a
     * managed context chain), a string.  Tasks collect, allocate and yield;
     * one is cancelled before it runs; a second run-tasks reuses a finished
     * slot.  keep runs at main depth 0, 10 and 20 (its caller's frames are
     * marked through the saved frame chain). */
    int e4_bad = 0;
    if (M1_MAX_TASKS != 3) e4_bad++;          /* h1..h3 fill every slot, so h4 reuses one */
    for (int d = 0; d <= 20; d += 10) {
        snprintf(src, sizeof src,
                 "[ ran2: 0  full: 0  reused: 0 "
                 "  keep: func [base] [ "
                 "    a: func [] [ + base 1 ] "
                 "    mk: func [x] [ func [] [ + x base ] ] "
                 "    b: mk 10 "
                 "    s: s/+ \"kept-\" s/from-int base "
                 "    h1: spawn [ repeat 3 [ collect  z: func [] [ 0 ]  yield ] ] "
                 "    h2: spawn [ set 'ran2 1 ] "
                 "    h3: spawn [ collect  w: [ 1 2 3 ]  yield  collect ] "
                 "    set 'full * * either < h1 0 [ 0 ] [ 1 ] either < h2 0 [ 0 ] [ 1 ] either < h3 0 [ 0 ] [ 1 ] "
                 "    task-cancel h2 "
                 "    run-tasks "
                 "    h4: spawn [ collect  q: func [] [ 9 ]  collect ]  set 'reused either < h4 0 [ 0 ] [ 1 ] "
                 "    run-tasks "
                 "    + a + b s/length s ] "
                 "  wrap: func [n] [ either > n 0 [ wrap - n 1 ] [ keep 7 ] ] "
                 "  r: wrap %d ]", d);
        int o = main_outcome(src);
        long r = o == 1 ? geti("r") : LONG_MIN;
        printf("    main depth %d: keep 7 -> %ld (want 31)\n", d, r);
        if (o != 1 || r != 31 || geti("ran2") != 0 || geti("full") != 1 || geti("reused") != 1) e4_bad++;
    }
    CHECK(e4_bad == 0,
          "E4 main-world locals (closures, nested-activation closure, string) survive collections in tasks, a cancelled task and slot reuse, at main depth 0/10/20");
    /* E4b: the minimal reproducer of the inherited defect (W5.1: 54) */
    int o4 = main_outcome("[ rec: func [n] [ k: func [] [ n ]  either > n 0 [ + rec - n 1  k ] [ collect z1: func [] [ 1 ] 0 ] ] "
                          "  mrec: func [n] [ k: func [] [ n ]  either > n 0 [ + mrec - n 1  k ] [ spawn [ rec 5 ]  run-tasks  0 ] ] "
                          "  r: mrec 10 ]");
    CHECK(o4 == 1 && geti("r") == 55, "E4 reproducer: a task's collection while main is 10 deep keeps main's closures (55; W5.1 gave 54)");
}

/* ---------------------------------------------------------------- F ---- */
/* GLON-C3-2E: run-tasks is main-world only.  From inside a task it raises
 * SIN! [task nested-run-tasks] before the scheduler record is written (it
 * used to overwrite the parked main world's record and run away). */
static void test_nested_run_tasks(void) {
    printf("F: nested run-tasks is rejected (C3-2E)\n");
    /* sched-ck: a checksum of the scheduler record's 8 cells, read from
     * inside the task before and after the rejected call */
    const char *src =
        "[ sched-at: raw 1 [ LIT 16 DIV LIT M1_SCHED_REC ADD @ LIT 16 MUL ARITY 1 EXIT ] "
        "  sched-ck: func [] [ + sched-at 0 + * 2 sched-at 1 + * 3 sched-at 2 + * 4 sched-at 3 "
        "                      + * 5 sched-at 4 + * 6 sched-at 5 + * 7 sched-at 6 * 8 sched-at 7 ] "
        "  ck-before: 0  ck-after: 1  nerr: none  after: 0  other: 0 "
        "  keep: func [base] [ "
        "    a: func [] [ + base 1 ] "
        "    h1: spawn [ set 'ck-before sched-ck  set 'nerr judge [ run-tasks ] "
        "                set 'ck-after sched-ck  yield  set 'after 1 ] "
        "    h2: spawn [ yield  set 'other 2 ] "
        "    run-tasks "
        "    a ] "
        "  wrap: func [n] [ either > n 0 [ wrap - n 1 ] [ keep 7 ] ] "
        "  r: wrap 5 ]";
    if (fresh()) { CHECK(0, "F session"); return; }
    int rc = run_src(src, "F1");
    CHECK(rc == 0 && geti("r") == 8, "F1 main (5 deep, in a function with locals) completes after a task's nested run-tasks: 8");
    CHECK(geti("either sin? nerr [ either = sin-id nerr 'nested-run-tasks [ either = sin-type nerr 'task [ 1 ] [ 0 ] ] [ 0 ] ] [ 0 ]") == 1,
          "F1 the nested call raised SIN! [task nested-run-tasks], caught by the task's judge");
    CHECK(geti("= ck-before ck-after") == 1 && geti("ck-before") != 0,
          "F1 the scheduler record is unchanged by the rejected call");
    CHECK(geti("after") == 1 && geti("other") == 2,
          "F1 the rejecting task yields and finishes; the other task runs");
    /* F2: unjudged in the task body: the task fails with that SIN! (left in
     * tasks/error), the main world carries on, and a later run-tasks from
     * the main world works (the slot is reused) */
    if (fresh()) { CHECK(0, "F2 session"); return; }
    rc = run_src("[ late: 0  t: spawn [ run-tasks  set 'late 1 ]  run-tasks  e: tasks/error "
                 "  u: spawn [ set 'late 2 ]  run-tasks ]", "F2");
    CHECK(rc == 0 && geti("either sin? e [ either = sin-id e 'nested-run-tasks [ 1 ] [ 0 ] ] [ 0 ]") == 1,
          "F2 an unjudged nested run-tasks fails that task with SIN! [task nested-run-tasks] in tasks/error");
    CHECK(geti("late") == 2 && geti("tasks/pending?") == 0,
          "F2 nothing after the rejected call ran; main's next run-tasks runs a new task in the reused slot");
}

int main(int argc, char **argv) {
    if (argc > 1) root = argv[1];
    if (argc > 2 && strcmp(argv[2], "negctl") == 0) {
        long g0, g1;
        int ok = resp_survives(&g0, &g1);
        printf("negctl: collections %ld; %s\n", g1 - g0, ok == 1 ? "RESP_SURVIVED" : "RESP_LOST");
        return 0;
    }
    printf("task-runtime: profile M1_MAX_TASKS=%d, task table %ld\n", (int)M1_MAX_TASKS, (long)M1_TASK_TABLE);
    test_scheduling();
    test_reuse_and_handles();
    test_resp();
    test_stacks();
    test_worst_case_and_gc();
    test_nested_run_tasks();
    printf("TASK_RUNTIME %s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
