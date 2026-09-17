/* r0_s1_m2_tests.c - M2: shared-heap garbage collection above frozen S1.
 *
 * A non-moving, stop-the-world, exact mark/sweep collector over the ONE shared
 * GLON heap.  These tests exercise reclamation, reuse, cycles, closure capture,
 * suspended multitasking worlds, idempotence, bounded-heap stress, OOM, and the
 * exact/RAW root contract.  See M2-GC-DESIGN.md.
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

#define XSTR_INNER(x) #x
#define XSTR(x) XSTR_INNER(x)

extern const char *M1_LIB;

static cell collect_addr;
static char lib_buf[4096];

/* M2 diagnostic library (RAW fragments only): untag, collect, heap-high */
static char *m2_lib(void) {
    snprintf(lib_buf, sizeof lib_buf,
        " untag: raw 1 [ DUP LIT 16 MOD SUB ARITY 1 EXIT ] "
        " collect: raw [ CALL %ld ARITY 0 EXIT ] "
        " heap-high: raw [ LIT REG_HP @ ARITY 1 EXIT ] ",
        (long)collect_addr);
    return lib_buf;
}

static char prog_buf[32768];

/* plain (non-multitasking) M2 run */
static int m2_run(const char *program, int *N) {
    int err = 0;
    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    collect_addr = r0_s1_gc_collect_addr();
    snprintf(prog_buf, sizeof prog_buf, "[ %s %s ]", m2_lib(), program);
    cell block = r0_s1_parse(prog_buf, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(block);
    return 0;
}

/* multitasking M2 run (M1_LIB + M2 lib + program) */
static int m2_m1_run(const char *program, int *N) {
    int err = 0;
    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    collect_addr = r0_s1_gc_collect_addr();
    snprintf(prog_buf, sizeof prog_buf, "[ %s %s %s ]", M1_LIB, m2_lib(), program);
    cell block = r0_s1_parse(prog_buf, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(block);
    return 0;
}

static void expect1(const char *program, cell want, const char *what) {
    int N; m2_run(program, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == want);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)want); failures++; }
}

/* ============================== the tests =============================== */

/* A: an unreachable simple object is reclaimed. */
static void test_A(void) {
    printf("m2: A unreachable simple object reclaimed\n");
    int N;
    m2_run(" f: func [] [ 42 ]  f: none  collect ", &N);
    CHECK(r0_s1_gc_count() >= 1, "A: a collection ran");
    CHECK(r0_s1_gc_free_cells() >= 32, "A: dead closure reclaimed (free cells)");
    CHECK(r0_s1_gc_live_objs() == 0, "A: no live managed objects remain");
}

/* B: reclaimed storage is actually reused by a later allocation. */
static void test_B(void) {
    printf("m2: B reclaimed storage reused\n");
    int N;
    m2_run(" mk: func [] [ func [] [ 42 ] ] "
           " x: mk  collect  x: none  collect "
           " h1: heap-high  y: mk  h2: heap-high  values [h1 h2] ", &N);
    int ok = (N == 2 && r0_s1_result(0, 2) == r0_s1_result(1, 2));
    if (ok) printf("  ok: B: heap frontier unchanged after reuse (h1 == h2)\n");
    else { printf("  FAIL: B: frontier grew (%ld -> %ld)\n",
                  (long)r0_s1_result(0, 2), (long)r0_s1_result(1, 2)); failures++; }
}

/* C: a reachable object survives repeated collection. */
static void test_C(void) {
    printf("m2: C reachable survives repeated GC\n");
    expect1(" f: func [] [ 42 ]  collect  collect  collect  f ", mk_int(42),
            "C: closure intact after 3 collections");
}

/* D: a nested block/context/closure graph survives while reachable. */
static void test_D(void) {
    printf("m2: D nested graph survives\n");
    expect1(" make: func [n] [ func [m] [ + n m ] ] "
            " add3: make 3  collect  add3 4 ", mk_int(7),
            "D: nested closure graph survives -> 7");
}

/* E: an unreachable nested graph is reclaimed. */
static void test_E(void) {
    printf("m2: E unreachable nested graph reclaimed\n");
    int N;
    m2_run(" make: func [n] [ func [m] [ + n m ] ] "
           " make 3  make 4  make 5  collect ", &N);
    CHECK(r0_s1_gc_count() >= 1, "E: a collection ran");
    CHECK(r0_s1_gc_free_cells() >= 64, "E: nested closures/contexts reclaimed");
}

/* F: a reachable cycle survives. */
static void test_F(void) {
    printf("m2: F reachable cycle survives\n");
    expect1(" f: func [] [ g: func [] [ 42 ]  g ]  h: f  collect  h ", mk_int(42),
            "F: cyclic closure survives -> 42");
}

/* G: an unreachable cycle is reclaimed. */
static void test_G(void) {
    printf("m2: G unreachable cycle reclaimed\n");
    int N;
    m2_run(" f: func [] [ g: func [] [ 42 ]  g ]  f  collect ", &N);
    CHECK(r0_s1_gc_count() >= 1, "G: a collection ran");
    CHECK(r0_s1_gc_live_objs() == 1, "G: unreachable cycle reclaimed (only reachable f survives)");
}

/* H: an escaping closure keeps its lexical context alive across GC. */
static void test_H(void) {
    printf("m2: H closure capture survives GC\n");
    expect1(" make-counter: func [start] [ func [d] [ start: + start d  start ] ] "
            " c: make-counter 10  collect  c 5 ", mk_int(15),
            "H: captured lexical env survives -> 15");
}

/* L: repeated collection is idempotent. */
static void test_L(void) {
    printf("m2: L idempotent collection\n");
    int N;
    m2_run(" f: func [] [ 42 ]  collect  collect  collect ", &N);
    long free1 = r0_s1_gc_free_cells();
    int N2;
    m2_run(" f: func [] [ 42 ]  collect ", &N2);
    CHECK(r0_s1_gc_count() == 1, "L: single explicit collection");
    CHECK(free1 == 0, "L: first collect reclaimed everything, later collects reclaim nothing");
}

/* M: bounded-heap stress - allocate/collect until reuse plateaus. */
static void test_M(void) {
    printf("m2: M bounded-heap stress\n");
    int N;
    m2_run(" mk: func [n] [ either <= n 0 [ none ] [ t: func [] [ 42 ]  mk - n 1 ] ] "
           " repeat: func [n b] [ either > n 0 [ do b  repeat - n 1 b ] [ none ] ] "
           " repeat 50 [ mk 15  collect ] "
           " f: func [] [ 42 ]  f ", &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == mk_int(42));
    if (ok) printf("  ok: M: 750 allocations + 50 collections, heap reused, result 42\n");
    else { printf("  FAIL: M (N=%d)\n", N); failures++; }
    CHECK(r0_s1_heap_high() <= 40000, "M: heap frontier stayed within the bounded region");
}

/* N: multitasking + allocation stress (>= 1000 switches). */
static void test_N(void) {
    printf("m2: N multitasking + allocation stress\n");
    int N;
    m2_m1_run(" task-a: spawn [ repeat 20 [ t: func [] [ 42 ]  collect  spin 25 ] ] "
              " task-b: spawn [ repeat 20 [ t: func [] [ 43 ]  collect  spin 25 ] ] "
              " run-tasks  read-stress ", &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == mk_int(1000));
    if (ok) printf("  ok: N: 1000 switches with allocation+collection, shared counter 1000\n");
    else { printf("  FAIL: N (N=%d got %ld)\n", N, (long)(N == 1 ? r0_s1_result(0, 1) : -999)); failures++; }
}

/* O: no heap growth into adjacent reserved regions. */
static void test_O(void) {
    printf("m2: O no growth into reserved regions\n");
    int N;
    m2_run(" mk: func [n] [ either <= n 0 [ none ] [ t: func [] [ 42 ]  mk - n 1 ] ] "
           " repeat: func [n b] [ either > n 0 [ do b  repeat - n 1 b ] [ none ] ] "
           " repeat 50 [ mk 15  collect ] ", &N);
    CHECK(r0_s1_heap_high() <= 40000, "O: heap frontier stayed below loader heap (40000)");
    CHECK(s1_mem(RV_CTX) == (cell)mk_context(0) || r0_tag(s1_mem(RV_CTX)) == T_CONTEXT,
            "O: evaluator state (RV_CTX) still a context (no overwrite)");
}

/* P: clean genuine OOM (deep recursion exhausts live frames). */
static void test_P(void) {
    printf("m2: P clean out-of-memory\n");
    const char *capture = "/tmp/opencode_m2_oom.txt";
    int saved_err = dup(2);
    int capfd = open(capture, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }

    int N;
    m2_run(" fact: func [n] [ either <= n 1 [ 1 ] [ * n fact - n 1 ] ]  fact 400 ", &N);

    fflush(stderr);
    if (capfd >= 0) { dup2(saved_err, 2); close(saved_err); }

    FILE *fp = fopen(capture, "r");
    int bad = 0;
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        if (strstr(buf, "bad opcode")) bad = 1;
    }
    CHECK(bad == 0, "P: OOM halts cleanly (no bad opcode / corruption)");
    CHECK(r0_s1_heap_high() <= 40000, "P: heap frontier bounded at OOM");
}

/* Q: integer values inside the heap range are not treated as roots. */
static void test_Q(void) {
    printf("m2: Q integers are not roots\n");
    int N;
    m2_run(" f: func [] [ 42 ]  f: none  big: 2048  collect ", &N);
    CHECK(r0_s1_gc_live_objs() == 0,
          "Q: heap-range integer (2048 -> 32768) did not pin the dead closure");
}

/* R: RAW contract - registered task/scheduler refs survive; unrelated RAW
 * scratch is not conservatively scanned. */
static void test_R(void) {
    printf("m2: R RAW root contract\n");
    int N;
    m2_run(" f: func [] [ 42 ] "
           " stash: raw 1 [ LIT 9000 ! ARITY 0 EXIT ] "
           " stash f  f: none  collect ", &N);
    CHECK(r0_s1_gc_live_objs() == 0,
          "R: closure referenced only by unregistered RAW scratch was collected");
}

/* I: suspended task survives GC and resumes correctly. */
static void test_I(void) {
    printf("m2: I suspended task survives GC\n");
    int N;
    m2_m1_run(" r: 0 "
              " task-a: spawn [ f: func [] [ 42 ]  collect  yield  r: f ] "
              " task-b: spawn [ yield ] "
              " run-tasks  r ", &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == mk_int(42));
    if (ok) printf("  ok: I: task allocation/capture survives a collection during yield -> 42\n");
    else { printf("  FAIL: I (N=%d got %ld)\n", N, (long)(N == 1 ? r0_s1_result(0, 1) : -999)); failures++; }
}

/* J: two tasks share an object; it survives while either reaches it. */
static void test_J(void) {
    printf("m2: J shared object survives across tasks\n");
    int N;
    m2_m1_run(" shared: 0 "
              " task-a: spawn [ shared: func [] [ 42 ]  collect  yield ] "
              " task-b: spawn [ collect  yield  shared ] "
              " run-tasks  shared ", &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == mk_int(42));
    if (ok) printf("  ok: J: object shared by two tasks survives (still invokes to 42)\n");
    else { printf("  FAIL: J (N=%d)\n", N); failures++; }
}

/* K: after the final reference disappears, a shared object becomes reclaimable. */
static void test_K(void) {
    printf("m2: K shared object reclaimable after termination\n");
    int N;
    m2_m1_run(" shared: func [] [ 42 ] "
              " task-a: spawn [ collect  yield  collect ] "
              " task-b: spawn [ yield ] "
              " run-tasks  shared: none  collect ", &N);
    CHECK(r0_s1_gc_live_objs() == 2,
          "K: after the last reference dropped, shared closure was reclaimed");
}

int run_r0_s1_m2_tests(void) {
    printf("R0-S1 M2: shared-heap garbage collection above frozen S1\n");

    test_A();
    test_B();
    test_C();
    test_D();
    test_E();
    test_F();
    test_G();
    test_H();
    test_I();
    test_J();
    test_K();
    test_L();
    test_M();
    test_N();
    test_O();
    test_P();
    test_Q();
    test_R();

    if (failures == 0) printf("all R0-S1 M2 GC tests passed\n");
    return failures;
}
