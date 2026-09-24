/* r0_s1_m1_tests.c - M1: cooperative multitasking above frozen R0/S1.
 *
 * Multiple independent R0/GLON computations (tasks) are suspended, scheduled
 * and resumed WITHOUT modifying S1, adding an eighth primitive, changing the
 * evaluator, or adding task semantics to C/HOST/JavaScript.
 *
 * A task is a suspended R0 execution world.  Its saved state is:
 *
 *     [ resume_ip, SP, RP, RV_CUR, RV_END, RV_CTX, RV_BLK, RV_FRAME ]
 *
 * plus a `state` cell (0=runnable, 1=finished, 2=empty).  REG_HP is GLOBAL
 * allocator state and is deliberately NOT saved/restored per task: all tasks
 * share ONE monotonically advancing heap, exactly as the D1 hardening audit
 * requires.  Each task has its own private DS and RS region (carved out of the
 * free region above the loader heap), so task stacks never overlap.
 *
 * Scheduling policy (round-robin over a fixed task table) and the allocation-
 * free scheduler loop live in RAW S1 fragments; the user-facing words
 * `spawn`/`repeat` are ordinary R0 `func`s.  No task semantics exist in
 * r0_s1_runtime.c / r0_s1.h / s1.c / HOST / JavaScript.
 *
 * See M1-MULTITASKING-RESULTS.md for the full memory map and design notes.
 */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

/* XSTR turns a plain integer macro into a string literal so the RAW source
 * below can embed the layout constants as ordinary integer operands (the RAW
 * assembler only resolves symbolic ABI names + integer literals).  This keeps
 * a single source of truth for every address. */
#define XSTR_INNER(x) #x
#define XSTR(x) XSTR_INNER(x)

/* ---- the M1 library: RAW mechanism + R0 policy --------------------------- */

const char *M1_LIB =
    /* mnew-task (RAW, arity 3): (body do-word finish-word -- handle)
     * Finds the first empty table slot, allocates a wrapper block
     * [ do body task-finish ] from the SHARED heap, seeds the task record's
     * initial machine state (resume_ip = main entry, private DS/RS, RV_* at the
     * wrapper, RV_CTX = the caller's context, RV_FRAME = 0), and returns the
     * handle mk_int(record). */
    " mnew-task: raw 3 [ "
    "   LIT " XSTR(M1_S2) " ! "
    "   LIT " XSTR(M1_S1) " ! "
    /* escape law (5), task transport: ESC_TRANSPORT checks the TAGGED body on
     * the stack top before it is untagged and before any task-table change */
    "   CALL ESC_TRANSPORT "
    "   DUP LIT 16 MOD SUB LIT " XSTR(M1_S0) " ! "
    "   LIT 0 LIT " XSTR(M1_S3) " ! "
    "   Lfind: "
    "   LIT " XSTR(M1_S3) " @ LIT " XSTR(M1_MAX_TASKS) " LT ZBRANCH Lfull "
    "   LIT " XSTR(M1_S3) " @ LIT 16 MUL LIT " XSTR(M1_TASK_TABLE) " ADD LIT " XSTR(M1_S4) " ! "
    "   LIT " XSTR(M1_S4) " @ LIT 8 ADD @ LIT " XSTR(TASK_EMPTY) " EQ ZBRANCH Lnext "
    "   BRANCH Lmake "
    "   Lnext: "
    "   LIT " XSTR(M1_S3) " @ LIT 1 ADD LIT " XSTR(M1_S3) " ! "
    "   BRANCH Lfind "
    "   Lfull: "
    "   LIT -1 LIT 16 MUL ARITY 1 EXIT "
    "   Lmake: "
    "   LIT " XSTR(M1_S4) " @ LIT " XSTR(M1_WRAPPER_DELTA) " ADD LIT " XSTR(M1_S5) " ! "
    "   LIT 3 LIT " XSTR(M1_S5) " @ ! "
    "   LIT 0 LIT " XSTR(M1_S5) " @ LIT 1 ADD ! "
    "   LIT " XSTR(M1_S1) " @ LIT " XSTR(M1_S5) " @ LIT 2 ADD ! "
    "   LIT " XSTR(M1_S0) " @ LIT 6 ADD LIT " XSTR(M1_S5) " @ LIT 3 ADD ! "
    "   LIT " XSTR(M1_S2) " @ LIT " XSTR(M1_S5) " @ LIT 4 ADD ! "
    "   LIT " XSTR(M1_MAIN_ENTRY_CELL) " @ LIT " XSTR(M1_S4) " @ ! "
    "   LIT " XSTR(M1_S3) " @ LIT " XSTR(M1_TASK_CELLS) " MUL LIT " XSTR(M1_ARENA_BASE) " ADD LIT " XSTR(M1_DS_OFF) " ADD LIT " XSTR(M1_S4) " @ LIT " XSTR(TREC_SP) " ADD ! "
    "   LIT " XSTR(M1_S3) " @ LIT " XSTR(M1_TASK_CELLS) " MUL LIT " XSTR(M1_ARENA_BASE) " ADD LIT " XSTR(M1_RS_OFF) " ADD LIT " XSTR(M1_S4) " @ LIT " XSTR(TREC_RP) " ADD ! "
    "   LIT " XSTR(M1_S5) " @ LIT 2 ADD LIT " XSTR(M1_S4) " @ LIT " XSTR(TREC_CUR) " ADD ! "
    "   LIT " XSTR(M1_S5) " @ LIT 5 ADD LIT " XSTR(M1_S4) " @ LIT " XSTR(TREC_END) " ADD ! "
    "   LIT RV_CTX @ LIT " XSTR(M1_S4) " @ LIT " XSTR(TREC_CTX) " ADD ! "
    "   LIT " XSTR(M1_S5) " @ LIT " XSTR(M1_S4) " @ LIT " XSTR(TREC_BLK) " ADD ! "
    "   LIT 0 LIT " XSTR(M1_S4) " @ LIT " XSTR(TREC_FRAME) " ADD ! "
    "   LIT " XSTR(TASK_RUNNABLE) " LIT " XSTR(M1_S4) " @ LIT " XSTR(TREC_STATE) " ADD ! "
    "   LIT " XSTR(M1_S4) " @ LIT 16 MUL ARITY 1 EXIT ] "

    /* yield (RAW, arity 0): task -> scheduler.  Pushes its own result [NONE,1]
     * (the suspended `yield` call returns NONE to the task on resume), saves the
     * task's resumable state into the current record, restores the scheduler
     * world, and jumps to the scheduler resume point.  HP is untouched. */
    " yield: raw [ "
    "   NONE LIT 16 "
    "   LIT REG_RP @ @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_IP) " ADD ! "
    "   LIT REG_SP @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_SP) " ADD ! "
    "   LIT REG_RP @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_RP) " ADD ! "
    "   LIT RV_CUR @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_CUR) " ADD ! "
    "   LIT RV_END @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_END) " ADD ! "
    "   LIT RV_CTX @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_CTX) " ADD ! "
    "   LIT RV_BLK @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_BLK) " ADD ! "
    "   LIT RV_FRAME @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_FRAME) " ADD ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 1 ADD @ LIT REG_SP ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 2 ADD @ LIT REG_RP ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 3 ADD @ LIT RV_CUR ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 4 ADD @ LIT RV_END ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 5 ADD @ LIT RV_CTX ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 6 ADD @ LIT RV_BLK ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 7 ADD @ LIT RV_FRAME ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 0 ADD @ LIT REG_IP ! ] "

    /* task-finish (RAW, arity 0): task -> scheduler, marking the current task
     * finished.  Does not save the task's state (it will never be resumed). */
    " task-finish: raw [ "
    "   LIT " XSTR(TASK_FINISHED) " LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_STATE) " ADD ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 1 ADD @ LIT REG_SP ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 2 ADD @ LIT REG_RP ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 3 ADD @ LIT RV_CUR ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 4 ADD @ LIT RV_END ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 5 ADD @ LIT RV_CTX ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 6 ADD @ LIT RV_BLK ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 7 ADD @ LIT RV_FRAME ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 0 ADD @ LIT REG_IP ! ] "

    /* run-tasks (RAW, arity 0): the round-robin scheduler loop.  On entry it
     * saves the scheduler world's state (resume_ip = the loop's own resume
     * point) and then, forever: picks the next runnable task (round-robin via
     * the cursor), switches into it; each task yields/finishes back to the
     * resume point, and the loop picks the next one.  When no task is runnable
     * it returns [NONE,1] to the caller.  Allocation-free. */
    " run-tasks: raw [ "
    "   LIT REG_IP @ LIT 11 ADD LIT " XSTR(M1_SCHED_REC) " LIT 0 ADD ! "
    "   LIT REG_SP @ LIT " XSTR(M1_SCHED_REC) " LIT 1 ADD ! "
    "   LIT REG_RP @ LIT " XSTR(M1_SCHED_REC) " LIT 2 ADD ! "
    "   LIT RV_CUR @ LIT " XSTR(M1_SCHED_REC) " LIT 3 ADD ! "
    "   LIT RV_END @ LIT " XSTR(M1_SCHED_REC) " LIT 4 ADD ! "
    "   LIT RV_CTX @ LIT " XSTR(M1_SCHED_REC) " LIT 5 ADD ! "
    "   LIT RV_BLK @ LIT " XSTR(M1_SCHED_REC) " LIT 6 ADD ! "
    "   LIT RV_FRAME @ LIT " XSTR(M1_SCHED_REC) " LIT 7 ADD ! "
    "   LIT " XSTR(M1_CURSOR) " @ LIT " XSTR(M1_S7) " ! "
    "   LIT 0 LIT " XSTR(M1_S8) " ! "
    "   Lscan: "
    "   LIT " XSTR(M1_S8) " @ LIT " XSTR(M1_MAX_TASKS) " LT ZBRANCH Ldone "
    "   LIT " XSTR(M1_S7) " @ LIT 16 MUL LIT " XSTR(M1_TASK_TABLE) " ADD LIT " XSTR(M1_S6) " ! "
    "   LIT " XSTR(M1_S6) " @ LIT 8 ADD @ LIT " XSTR(TASK_RUNNABLE) " NE ZBRANCH Lfound "
    "   Lskip: "
    "   LIT " XSTR(M1_S7) " @ LIT 1 ADD LIT " XSTR(M1_MAX_TASKS) " MOD LIT " XSTR(M1_S7) " ! "
    "   LIT " XSTR(M1_S8) " @ LIT 1 ADD LIT " XSTR(M1_S8) " ! "
    "   BRANCH Lscan "
    "   Ldone: "
    "   NONE LIT 16 EXIT "
    "   Lfound: "
    "   LIT " XSTR(M1_S6) " @ LIT " XSTR(M1_CUR_TASK) " ! "
    "   LIT " XSTR(M1_S7) " @ LIT 1 ADD LIT " XSTR(M1_MAX_TASKS) " MOD LIT " XSTR(M1_CURSOR) " ! "
    "   LIT " XSTR(M1_CUR_TASK) " @ LIT 1 ADD @ LIT REG_SP ! "
    "   LIT " XSTR(M1_CUR_TASK) " @ LIT 2 ADD @ LIT REG_RP ! "
    "   LIT " XSTR(M1_CUR_TASK) " @ LIT 3 ADD @ LIT RV_CUR ! "
    "   LIT " XSTR(M1_CUR_TASK) " @ LIT 4 ADD @ LIT RV_END ! "
    "   LIT " XSTR(M1_CUR_TASK) " @ LIT 5 ADD @ LIT RV_CTX ! "
    "   LIT " XSTR(M1_CUR_TASK) " @ LIT 6 ADD @ LIT RV_BLK ! "
    "   LIT " XSTR(M1_CUR_TASK) " @ LIT 7 ADD @ LIT RV_FRAME ! "
    "   LIT " XSTR(M1_CUR_TASK) " @ LIT 0 ADD @ LIT REG_IP ! ] "

    /* read-stress (RAW, arity 0): read the shared stress counter as an R0 int */
    " read-stress: raw [ LIT " XSTR(M1_STRESS_CNT) " @ LIT 16 MUL ARITY 1 EXIT ] "

    /* spin (RAW, arity 1): yield n times, incrementing the shared counter each
     * time.  Allocation-free stress driver: it keeps its counter on the task's
     * own data stack (so it survives context switches per-task), and inlines
     * the context-switch.  The loop-top address is captured once into M1_S9. */
    " spin: raw 1 [ "
    "   LIT 16 DIV "
    "   LIT REG_IP @ LIT 7 ADD LIT " XSTR(M1_S9) " ! "
    "   Lspinloop: "
    "   DUP LIT 0 GT ZBRANCH Lspindone "
    "   LIT 1 SUB "
    "   LIT " XSTR(M1_STRESS_CNT) " @ LIT 1 ADD LIT " XSTR(M1_STRESS_CNT) " ! "
    "   LIT " XSTR(M1_S9) " @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_IP) " ADD ! "
    "   LIT REG_SP @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_SP) " ADD ! "
    "   LIT REG_RP @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_RP) " ADD ! "
    "   LIT RV_CUR @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_CUR) " ADD ! "
    "   LIT RV_END @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_END) " ADD ! "
    "   LIT RV_CTX @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_CTX) " ADD ! "
    "   LIT RV_BLK @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_BLK) " ADD ! "
    "   LIT RV_FRAME @ LIT " XSTR(M1_CUR_TASK) " @ LIT " XSTR(TREC_FRAME) " ADD ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 1 ADD @ LIT REG_SP ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 2 ADD @ LIT REG_RP ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 3 ADD @ LIT RV_CUR ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 4 ADD @ LIT RV_END ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 5 ADD @ LIT RV_CTX ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 6 ADD @ LIT RV_BLK ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 7 ADD @ LIT RV_FRAME ! "
    "   LIT " XSTR(M1_SCHED_REC) " LIT 0 ADD @ LIT REG_IP ! "
    "   Lspindone: "
    "   DROP ARITY 0 EXIT ] "

    /* ---- R0 policy layer (ordinary funcs) -------------------------------- */
    " spawn: func [body] [ mnew-task body 'do 'task-finish ] "
    " repeat: func [n b] [ either > n 0 [ do b  repeat - n 1 b ] [ none ] ] ";

/* ---- harness ------------------------------------------------------------- */

static char m1_buf[16384];

/* Seed the scheduler environment and run an M1 program (library + body).
 * This is the run-driver boundary only: it installs the evaluator entry point,
 * resets the round-robin cursor and marks every task slot empty -- exactly the
 * same kind of environment seeding the D1 debugger driver performs.  No
 * scheduling policy lives here. */
static int m1_run(const char *program, int *N) {
    int err = 0;
    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    snprintf(m1_buf, sizeof m1_buf, "[ %s %s ]", M1_LIB, program);
    cell block = r0_s1_parse(m1_buf, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(block);
    return 0;
}

static void expect1(const char *program, cell want, const char *what) {
    int N; m1_run(program, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == want);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)want); failures++; }
}

static void expectN(const char *program, int n, const cell *vals, const char *what) {
    int N; m1_run(program, &N);
    int ok = (N == n);
    if (ok) for (int i = 0; i < n; i++) if (r0_s1_result(i, N) != vals[i]) ok = 0;
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d want %d)\n", what, N, n); failures++; }
}

/* ============================== the tests ================================= */

/* A: two tasks alternate across explicit yield (trace encodes the order). */
static void test_two_alternate(void) {
    printf("m1: A two tasks alternate across yield\n");
    const char *p =
        " trace: 0 "
        " task-a: spawn [ repeat 3 [ trace: + * trace 10 1  yield ] ] "
        " task-b: spawn [ repeat 3 [ trace: + * trace 10 2  yield ] ] "
        " run-tasks "
        " trace ";
    expect1(p, mk_int(121212), "A: interleaving 1,2,1,2,1,2 -> 121212");
}

/* B: three tasks run round-robin and all terminate. */
static void test_three_round_robin(void) {
    printf("m1: B three tasks round-robin\n");
    const char *p =
        " trace: 0 "
        " task-a: spawn [ trace: + * trace 10 1  yield  trace: + * trace 10 4 ] "
        " task-b: spawn [ trace: + * trace 10 2  yield  trace: + * trace 10 5 ] "
        " task-c: spawn [ trace: + * trace 10 3  yield  trace: + * trace 10 6 ] "
        " run-tasks "
        " trace ";
    expect1(p, mk_int(123456), "B: round-robin A,B,C,A,B,C -> 123456");
}

/* C: yield from inside several unaware function calls. */
static void test_unaware_calls(void) {
    printf("m1: C yield inside unaware call chain\n");
    const char *p =
        " f: func [x] [ yield  x ] "
        " g: func [x] [ + f x 1 ] "
        " h: func [x] [ g x ] "
        " a-result: 0 "
        " task-a: spawn [ a-result: h 10 ] "
        " task-b: spawn [ yield ] "
        " run-tasks "
        " a-result ";
    expect1(p, mk_int(11), "C: h->g->f yield, resume -> 11");
}

/* D: recursive calls survive yield/resume. */
static void test_recursion(void) {
    printf("m1: D recursion survives yield\n");
    const char *p =
        " fact: func [n] [ either <= n 1 [ 1 ] [ yield  * n fact - n 1 ] ] "
        " r: 0 "
        " task-a: spawn [ r: fact 3 ] "
        " task-b: spawn [ yield ] "
        " run-tasks "
        " r ";
    expect1(p, mk_int(6), "D: fact 3 across yields == 6");
}

/* E: lexical closures survive yield/resume. */
static void test_closure(void) {
    printf("m1: E lexical closure survives yield\n");
    const char *p =
        " make-counter: func [start] [ func [delta] [ start: + start delta  yield  start ] ] "
        " c: make-counter 10 "
        " r: 0 "
        " task-a: spawn [ r: c 5 ] "
        " task-b: spawn [ yield ] "
        " run-tasks "
        " r ";
    expect1(p, mk_int(15), "E: closure captured start survives -> 15");
}

/* F: each task has independent locals/activation frames. */
static void test_independent_locals(void) {
    printf("m1: F independent locals per task\n");
    const char *p =
        " f: func [x] [ yield  x ] "
        " ra: 0  rb: 0 "
        " task-a: spawn [ ra: f 1 ] "
        " task-b: spawn [ rb: f 2 ] "
        " run-tasks "
        " values [ra rb] ";
    cell v[2] = { mk_int(1), mk_int(2) };
    expectN(p, 2, v, "F: independent locals -> [1 2]");
}

/* G: tasks intentionally share ordinary heap state. */
static void test_shared_state(void) {
    printf("m1: G shared heap state\n");
    const char *p =
        " shared: 0 "
        " task-a: spawn [ shared: + shared 1  yield  shared: + shared 100 ] "
        " task-b: spawn [ shared: + shared 2  yield  shared: + shared 200 ] "
        " run-tasks "
        " shared ";
    expect1(p, mk_int(303), "G: shared counter -> 303");
}

/* H: heap allocation by alternating tasks is monotonic; closures created by
 * one task are not overwritten by another's allocations. */
static void test_heap_monotonic(void) {
    printf("m1: H closures survive interleaved allocation\n");
    const char *p =
        " make: func [n] [ func [] [ n ] ] "
        " ra: 0  rb: 0 "
        " task-a: spawn [ c1: make 111  spin 50  ra: c1 ] "
        " task-b: spawn [ c2: make 222  spin 50  rb: c2 ] "
        " run-tasks "
        " values [ra rb read-stress] ";
    cell v[3] = { mk_int(111), mk_int(222), mk_int(100) };
    expectN(p, 3, v, "H: closures intact (111,222), 100 switches");
}

/* I: stress - 1000 context switches, clean stacks, no diagnostics. */
static void test_stress(void) {
    printf("m1: I stress (1000 context switches)\n");
    const char *p =
        " task-a: spawn [ spin 500 ] "
        " task-b: spawn [ spin 500 ] "
        " run-tasks "
        " read-stress ";
    int N;
    m1_run(p, &N);
    cell result = (N == 1) ? r0_s1_result(0, 1) : -999;
    CHECK(N == 1 && result == mk_int(1000),
          "I: 1000 switches, shared counter == 1000");
    CHECK(r0_s1_rp_end() == r0_s1_rp_start(),
          "I: scheduler RP returned to baseline");
    CHECK(r0_s1_sp_end() == r0_s1_sp_start() - 2,
          "I: scheduler SP holds exactly the result set");
    printf("    [stress] sp %ld->%ld  rp %ld->%ld (N=%d)\n",
           (long)r0_s1_sp_start(), (long)r0_s1_sp_end(),
           (long)r0_s1_rp_start(), (long)r0_s1_rp_end(), N);
}

/* J: single continuous run with interleaved R0 allocations AND many raw
 * switches, to prove the two paths coexist without heap regression. */
static void test_mixed(void) {
    printf("m1: J mixed R0 closures + raw switches\n");
    const char *p =
        " make: func [n] [ func [] [ n ] ] "
        " ra: 0  rb: 0 "
        " task-a: spawn [ c1: make 111  spin 200  ra: c1 ] "
        " task-b: spawn [ c2: make 222  spin 200  rb: c2 ] "
        " run-tasks "
        " values [ra rb read-stress] ";
    cell v[3] = { mk_int(111), mk_int(222), mk_int(400) };
    expectN(p, 3, v, "J: closures intact + 400 switches");
}

/* mechanical audit: the frozen runtime/host gained no multitasking semantics */
static void audit_runtime_clean(void) {
    /* M2's collector legitimately *references* task records as GC roots (GC is
     * global and sees every suspended task), but it must not add any task
     * SCHEDULING semantics: no spawn/yield, no round-robin, no context switch. */
    static const char *forbidden[] = {
        "spawn", "yield", "round-robin", "round robin",
        "context-switch", "context switch"
    };
    int nbad = 6, violations = 0;
    const char *files[] = { "r0_s1_runtime.c", "r0_s1.h", "s1.c" };
    for (int f = 0; f < 3; f++) {
        FILE *fp = fopen(files[f], "r");
        if (!fp) continue;
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        for (int i = 0; i < nbad; i++)
            if (strstr(buf, forbidden[i])) {
                printf("  FAIL: frozen file %s mentions '%s'\n", files[f], forbidden[i]);
                violations++;
            }
    }
    CHECK(violations == 0, "frozen runtime/host contains no task scheduling semantics");
}

int run_r0_s1_m1_tests(void) {
    printf("R0-S1 M1: cooperative multitasking above frozen R0/S1\n");

    const char *capture = "/tmp/opencode_m1_stderr.txt";
    int saved_err = dup(2);
    int capfd = open(capture, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }

    test_two_alternate();
    test_three_round_robin();
    test_unaware_calls();
    test_recursion();
    test_closure();
    test_independent_locals();
    test_shared_state();
    test_heap_monotonic();
    test_stress();
    test_mixed();

    fflush(stderr);
    if (capfd >= 0) { dup2(saved_err, 2); close(saved_err); }

    FILE *fp = fopen(capture, "r");
    int bad = 0;
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        if (strstr(buf, "bad opcode")) { printf("  FAIL: unexpected 'bad opcode'\n"); bad = 1; }
        if (strstr(buf, "[dump]"))     { printf("  FAIL: unexpected '[dump]' (unbound/error)\n"); bad = 1; }
    }
    CHECK(bad == 0, "no bad-opcode / unbound-word diagnostics across all M1 tests");

    audit_runtime_clean();

    if (failures == 0) printf("all R0-S1 M1 multitasking tests passed\n");
    return failures;
}
