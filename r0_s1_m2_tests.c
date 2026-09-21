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

/* A test-scratch cell that is mechanically guaranteed to lie OUTSIDE the
 * emitted code region [256, 256+r0_s1_code_size()), so RAW fragments may use it
 * without corrupting the program. It lives in the free region just above the
 * code (well below the data stack), and the collector never scans it (it is not
 * a root and not inside [SP, DS_INIT)). */
static cell m2_scratch_cell(void) {
    r0_s1_init();                                   /* ensure the code is emitted */
    return (256 + r0_s1_code_size() + 64) & ~15L;   /* 16-aligned, above code end */
}

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

/* N2: recursion that comfortably exceeds the OLD 800-cell task RS but stays
 * below the enlarged 3200-cell RS must succeed (P3 stack contexts). */
static void test_N2(void) {
    printf("m2: N2 task recursion below the enlarged RS limit succeeds\n");
    int N;
    m2_m1_run(" deep: func [n] [ either <= n 0 [ spin 1 ] [ deep - n 1 ] ] "
              " task-a: spawn [ deep 30 ] "
              " task-b: spawn [ deep 30 ] "
              " run-tasks  read-stress ", &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == mk_int(2));
    if (ok) printf("  ok: N2: two tasks recurse 30 deep (past the old 800-cell RS) and complete\n");
    else { printf("  FAIL: N2 (N=%d got %ld)\n", N, (long)(N == 1 ? r0_s1_result(0, 1) : -999)); failures++; }
}

/* N3: recursion well past the RS limit must fail-stop cleanly (return-stack
 * guard), never overwrite a task's saved IP and later surface as bad opcode. */
static void test_N3(void) {
    printf("m2: N3 deliberate task RS overflow fails cleanly\n");
    const char *capture = "/tmp/opencode_m2_rsovf.txt";
    int saved_err = dup(2);
    int capfd = open(capture, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }
    int N;
    m2_m1_run(" deep: func [n] [ either <= n 0 [ 0 ] [ deep - n 1 ] ] "
              " task-a: spawn [ deep 60 ] "
              " run-tasks ", &N);
    fflush(stderr);
    if (capfd >= 0) { dup2(saved_err, 2); close(saved_err); }
    FILE *fp = fopen(capture, "r");
    int bad = 0, dump = 0;
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        if (strstr(buf, "bad opcode")) bad = 1;
        if (strstr(buf, "[dump]")) dump = 1;
    }
    CHECK(bad == 0, "N3: overflow fails cleanly (no bad opcode / no saved-IP corruption)");
    CHECK(dump == 1, "N3: overflow guard triggered (fail-stop, not silent corruption)");
}

/* N4: a neighbouring suspended task's saved state must survive an overflow in
 * another task (the guard halts before the arena is written). */
static void test_N4(void) {
    printf("m2: N4 neighbour task state intact across an overflow\n");
    int N;
    m2_m1_run(" deep: func [n] [ either <= n 0 [ 0 ] [ deep - n 1 ] ] "
              " task-b: spawn [ spin 10 ] "
              " task-a: spawn [ deep 60 ] "
              " run-tasks ", &N);
    /* task-b (slot 0) yields once, then task-a (slot 1) overflows and halts.
     * task-b's record must still be RUNNABLE with its SP/RP inside its arena. */
    int ok = 1;
    if (s1_mem(M1_TASK_TABLE + 0 * M1_TASK_REC_SIZE + TREC_STATE) != TASK_RUNNABLE) ok = 0;
    cell b_sp = s1_mem(M1_TASK_TABLE + 0 * M1_TASK_REC_SIZE + TREC_SP);
    cell b_rp = s1_mem(M1_TASK_TABLE + 0 * M1_TASK_REC_SIZE + TREC_RP);
    if (b_sp < M1_ARENA_BASE || b_sp >= M1_ARENA_BASE + M1_TASK_CELLS) ok = 0;
    if (b_rp < M1_ARENA_BASE || b_rp >= M1_ARENA_BASE + M1_TASK_CELLS) ok = 0;
    if (ok) printf("  ok: N4: suspended neighbour's record + arena intact after the other task overflowed\n");
    else { printf("  FAIL: N4 (state=%ld sp=%ld rp=%ld)\n",
                  (long)s1_mem(M1_TASK_TABLE + TREC_STATE), (long)b_sp, (long)b_rp); failures++; }
}

/* N5: suspending and resuming at substantial recursion depth stays correct. */
static void test_N5(void) {
    printf("m2: N5 suspend/resume at substantial recursion depth\n");
    int N;
    m2_m1_run(" deep: func [n] [ either <= n 0 [ spin 3 ] [ spin 1  deep - n 1 ] ] "
              " task-a: spawn [ deep 30 ] "
              " task-b: spawn [ spin 20 ] "
              " run-tasks  read-stress ", &N);
    /* task-a yields once per level (30) + 3 at the base; task-b yields 20. */
    int ok = (N == 1 && r0_s1_result(0, 1) == mk_int(53));
    if (ok) printf("  ok: N5: 30-deep recursion suspends/resumes across switches (counter 53)\n");
    else { printf("  FAIL: N5 (N=%d got %ld)\n", N, (long)(N == 1 ? r0_s1_result(0, 1) : -999)); failures++; }
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
    cell scratch = m2_scratch_cell();
    CHECK(scratch >= 256 + r0_s1_code_size(),
          "R: unregistered scratch cell lies outside emitted code");
    char prog[256];
    snprintf(prog, sizeof prog,
             " f: func [] [ 42 ] "
             " stash: raw 1 [ LIT %ld ! ARITY 0 EXIT ] "
             " stash f  f: none  collect ", (long)scratch);
    m2_run(prog, &N);
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

/* S: a managed value reachable only through a live non-GC activation frame's
 * saved context survives GC (the frame chain is traced as root storage), and is
 * reclaimed once the activation returns and the last reference drops. This is
 * the adversarial proof that stack frames are proper GC roots, not survivors. */
static void test_S(void) {
    printf("m2: S live frame is a GC root\n");
    /* outer binds v (a closure) in its child context and then calls the global
     * gc-now; during that collect v is reachable ONLY via outer's frame CTX. */
    expect1(" gc-now: func [] [ collect ] "
            " outer: func [] [ v: func [] [ 42 ]  gc-now  v ] "
            " outer ", mk_int(42),
            "S: value reachable only via a live frame survives GC");
    int N;
    m2_run(" gc-now: func [] [ collect ] "
           " outer: func [] [ v: func [] [ 42 ]  gc-now  v ] "
           " outer  collect ", &N);
    CHECK(r0_s1_gc_free_cells() >= 32,
           "S: value reclaimed once the frame is popped and the reference drops");
}

/* T: the G1 permanent emit buffer (mk_block(G1_OUT), payload 25344) is a
 * legitimate render result that stays live across a later GC. Its cells are all
 * tagged ints (rendered bytes), so mark_value must treat it as an opaque leaf.
 * Before the fix, this fail-stopped as "out-of-range T_BLOCK". */
static void test_T(void) {
    printf("m2: T permanent G1_OUT emit block survives collection\n");
    int N;
    m2_run(" mk-g1out: raw [ LIT G1_OUT LIT T_BLOCK ADD ARITY 1 EXIT ] "
           " x: mk-g1out collect x ", &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == mk_block(G1_OUT));
    if (ok) printf("  ok: T: mk_block(G1_OUT) survives collect (opaque leaf, no children)\n");
    else { printf("  FAIL: T (N=%d got %ld want %ld)\n", N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)mk_block(G1_OUT)); failures++; }
}

/* U: a forged T_BLOCK at an unrelated low address is still fail-stop corruption
 * (only the exact G1_OUT buffer is exempt, never arbitrary low blocks). */
static void test_U(void) {
    printf("m2: U forged low-address T_BLOCK is corruption\n");
    const char *capture = "/tmp/opencode_m2_forgeblk.txt";
    int saved_err = dup(2);
    int capfd = open(capture, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }

    int N;
    m2_run(" forge: raw [ LIT 16 LIT T_BLOCK ADD ARITY 1 EXIT ] "
           " x: forge collect ", &N);

    fflush(stderr);
    if (capfd >= 0) { dup2(saved_err, 2); close(saved_err); }

    FILE *fp = fopen(capture, "r");
    int bad = 0, dump = 0;
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        if (strstr(buf, "bad opcode")) bad = 1;
        if (strstr(buf, "[dump]")) dump = 1;
    }
    CHECK(bad == 0 && dump == 1,
          "U: forged low-address T_BLOCK still halts cleanly as corruption");
}

/* V: the collector must classify SP0 into an explicit valid region, never
 * derive a negative/out-of-range task slot from raw SP arithmetic. */
static int m2_set_sp_check(cell sp) {
    int N;
    char prog[256];
    snprintf(prog, sizeof prog,
             " set-sp: raw 1 [ LIT 16 DIV LIT REG_SP ! LIT 0 LIT REG_SP @ ! CALL %ld ARITY 0 EXIT ] set-sp %ld ",
             (long)collect_addr, (long)sp);
    m2_run(prog, &N);
    return r0_s1_ran_cleanly();
}

static void test_V(void) {
    printf("m2: V GC world classification by explicit SP range\n");
    CHECK(m2_set_sp_check(R0S1_DS_INIT),
          "V: SP == DS_INIT scans the main world (no fail-stop)");
    /* seed the main DS cells just below DS_INIT so a non-empty SP scans
     * well-formed tagged ints rather than stale cells */
    for (cell a = R0S1_DS_INIT - 64; a < R0S1_DS_INIT; a++) M[a] = 0;
    CHECK(m2_set_sp_check(R0S1_DS_INIT - 1),
          "V: a valid non-empty main-world SP scans the main world");    CHECK(!m2_set_sp_check(R0S1_DS_INIT + 2),
          "V: SP == DS_INIT+2 fail-stops (would otherwise give a negative slot)");
    CHECK(!m2_set_sp_check(M1_ARENA_BASE - 1),
          "V: SP just below the M1 arena fail-stops");
    CHECK(!m2_set_sp_check(M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS),
          "V: SP at the M1 arena end fail-stops");
    CHECK(!m2_set_sp_check(M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS + 64),
          "V: SP beyond the M1 arena fail-stops");
}

/* W: stack sentries -- a run that leaves SP/RP outside the legal main-world
 * region fires the C-level sentry. (The sentry reports the final register
 * state after the run, so each offending operation is exercised in its own
 * run; the "ARITY 0" result cell compensates one cell, hence the two-fold
 * operations below.) */
static int m2_sentry(const char *prog) {
    int N;
    m2_run(prog, &N);
    return r0_s1_stack_sentry_fired();
}

static void test_W(void) {
    printf("m2: W stack sentries detect main-world SP/RP bounds\n");
    CHECK(m2_sentry(" drop-empty: raw [ DROP DROP ARITY 0 EXIT ] drop-empty "),
          "W: DROP on empty main DS fires the sentry (underflow)");
    CHECK(m2_sentry(" add-empty: raw [ ADD ADD ARITY 0 EXIT ] add-empty "),
          "W: ADD needing 2 operands on empty main DS fires (underflow)");
    CHECK(m2_sentry(" ds-over: raw [ LIT 0 LIT 25060 ! "
            " L1: LIT 25060 @ LIT 10000 LT ZBRANCH Ldone LIT 0 LIT 25060 @ LIT 1 ADD LIT 25060 ! BRANCH L1 "
            " Ldone: ARITY 0 EXIT ] ds-over "),
          "W: main DS overflow (pushing past code) fires the sentry");
    CHECK(m2_sentry(" rpop-empty: raw [ LIT REG_RP @ @ LIT 24577 ! LIT 24577 LIT REG_RP ! ARITY 0 EXIT ] rpop-empty "),
          "W: RP raised above RS_INIT fires the sentry (underflow)");
    CHECK(m2_sentry(" rs-over: raw [ LIT 0 LIT 25060 ! "
            " L1: LIT 25060 @ LIT 9000 LT ZBRANCH Ldone LIT 0 >R LIT 25060 @ LIT 1 ADD LIT 25060 ! BRANCH L1 "
            " Ldone: ARITY 0 EXIT ] rs-over "),
          "W: main RS overflow (too many >R) fires the sentry");
    CHECK(!m2_sentry(" drop-valid: raw [ LIT 0 DROP ARITY 0 EXIT ] drop-valid "),
          "W: DROP on a non-empty main DS does not fire the sentry");
    CHECK(!m2_sentry(" r-valid: raw [ LIT 0 >R R> DROP ARITY 0 EXIT ] r-valid "),
          "W: balanced >R/R> does not fire the sentry");
}

/* X: task-world stack sentries (via the collector's RUNNABLE-task scan) and the
 * finished-task / task-slot-reuse convention. */
static void test_X(void) {
    printf("m2: X task stack sentries + finished-task convention\n");
    int N;

    /* task DS underflow: the task over-pops its DS; when the collector scans the
     * still-RUNNABLE task it must fail-stop rather than scan a bogus range. */
    m2_m1_run(" t-drop: raw [ DROP DROP ARITY 0 EXIT ] "
              " task-a: spawn [ t-drop t-drop t-drop collect ] "
              " run-tasks ", &N);
    CHECK(!r0_s1_ran_cleanly(),
          "X: task DS underflow fail-stops (SP above the task DS top)");

    /* finished-task convention: after run-tasks the task is FINISHED with a
     * two-cell residual on its DS (the suspended yield's [NONE,arity]) and an
     * unconsumed RS; the collector SKIPS finished tasks, so a following collect
     * must NOT treat those residuals as roots. */
    m2_m1_run(" task-a: spawn [ yield ] run-tasks collect ", &N);
    CHECK(r0_s1_ran_cleanly(), "X: finished task residuals are skipped by GC");
    {
        cell *rec = &M[M1_TASK_TABLE + 0 * M1_TASK_REC_SIZE];
        CHECK(rec[TREC_STATE] == TASK_FINISHED, "X: task is FINISHED after run-tasks");
        cell base = M1_ARENA_BASE;
        printf("  (finished task SP=%ld base+DS_OFF=%ld RP=%ld base+RS_OFF=%ld)\n",
               (long)rec[TREC_SP], (long)(base + M1_DS_OFF),
               (long)rec[TREC_RP], (long)(base + M1_RS_OFF));
    }

    /* slot reuse: spawn again into the same slot, run to FINISHED, force GC --
     * fresh SP/RP must be reinitialised and stale residuals must not become
     * roots or affect execution. */
    m2_m1_run(" task-a: spawn [ yield ] run-tasks "
              " task-a: spawn [ yield ] run-tasks collect ", &N);
    CHECK(r0_s1_ran_cleanly(), "X: slot reuse reinitialises SP/RP and survives GC");
    {
        cell *rec = &M[M1_TASK_TABLE + 0 * M1_TASK_REC_SIZE];
        cell base = M1_ARENA_BASE;
        CHECK(rec[TREC_STATE] == TASK_FINISHED, "X: reused task is FINISHED");
        CHECK(rec[TREC_SP] >= base && rec[TREC_SP] <= base + M1_DS_OFF,
              "X: reused task SP is inside the DS region");
        CHECK(rec[TREC_RP] >= base + M1_DS_OFF && rec[TREC_RP] <= base + M1_RS_OFF,
              "X: reused task RP is inside the RS region");
    }
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
    test_N2();
    test_N3();
    test_N4();
    test_N5();
    test_O();
    test_P();
    test_Q();
    test_R();
    test_S();
    test_T();
    test_U();
    test_V();
    test_W();
    test_X();

    if (failures == 0) printf("all R0-S1 M2 GC tests passed\n");
    return failures;
}
