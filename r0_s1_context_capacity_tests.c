/* r0_s1_context_capacity_tests.c - a full context fail-stops before any write.
 *
 * Every context has a fixed binding capacity: 256 for the global context,
 * R0S1_CTX_CAP (16) for a function activation's context. A new binding (a
 * set-word that finds no existing binding, or a bound parameter) is appended
 * at index `count`. When count == cap the runtime now fail-stops BEFORE writing:
 * no binding, hash entry or count changes, and nothing past the context (its
 * hash index, its escape-site tail, the loader heap or the return stack behind
 * it) is touched. The halt records R0S1_HALT_CONTEXT_FULL
 * (r0_s1_halt_reason); it is a machine resource ceiling, not a SIN!, so judge
 * cannot catch it. Capacities are deliberately unchanged. */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static void fresh(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}

#define HALTED   (-777777)
#define NOT_INT  (-888888)
#define BAD_PARSE (-999999)
static cell global_p;               /* global context payload (set by the first run) */
static long cellv(const char *src) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) return BAD_PARSE;
    int n = r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly()) return HALTED;
    if (!global_p) global_p = M[RV_CTX] & ~(cell)15;   /* a clean top-level run ends in the global context */
    if (n != 1) return NOT_INT;
    cell v = r0_s1_result(0, n);
    return (v & 15) == T_INT ? (long)int_val(v) : NOT_INT;
}
static int full_halt(void) {
    cell t;
    return !r0_s1_ran_cleanly() && r0_s1_halt_reason() == R0S1_HALT_CONTEXT_FULL
           && !r0_s1_uncaught_error(&t, NULL, NULL);
}

/* snapshot of the global context (header + data + hash) and the loader heap
 * that follows it up to the current loader frontier */
static cell snap[20000];
static int snap_n;
static void take_snapshot(void) {
    cell cap = M[global_p + CTX_CAP];
    cell from = global_p, to = M[GC_LOADER_HP];
    (void)cap;
    snap_n = (int)(to - from);
    memcpy(snap, &M[from], sizeof(cell) * (size_t)snap_n);
}
static int unchanged_since_snapshot(void) {
    return memcmp(snap, &M[global_p], sizeof(cell) * (size_t)snap_n) == 0;
}

int run_r0_s1_context_capacity_tests(void) {
    printf("R0-S1 context capacity (fail-stop before write)\n");

    /* ---- global context --------------------------------------------------- */
    fresh();
    global_p = 0;
    CHECK(cellv("[ keep: 21  kf: func [n] [ * n 2 ]  mk: func [n] [ func [] [ n: + n 1 ] ]  c: mk 0  0 ]") == 0,
          "G0: session state defined (keep, kf, a counter closure c)");
    cell cap = M[global_p + CTX_CAP];
    CHECK(cap == 256, "G1a: the global context's capacity is 256 (unchanged)");
    int added = 0;
    long r = 0;
    for (int k = 0; k < 400; k++) {
        char src[64];
        snprintf(src, sizeof src, "[ g%d: %d  0 ]", k, k);
        r = cellv(src);
        if (r == HALTED) break;
        added++;
    }
    CHECK(r == HALTED && full_halt(), "G1b: filling the global context ends in a CONTEXT_FULL fail-stop (not a SIN!)");
    CHECK(M[global_p + CTX_COUNT] == cap, "G1c: the global context filled to exactly its capacity (256 of 256)");
    {
        int ok = 1;
        for (int k = 0; k < added; k++) {
            char src[32];
            snprintf(src, sizeof src, "[ g%d ]", k);
            if (cellv(src) != k) ok = 0;
        }
        CHECK(ok && cellv("[ kf keep ]") == 42, "G3: every earlier binding is intact (all gK, kf keep -> 42)");
    }
    CHECK(cellv("[ keep: 5  keep ]") == 5, "G4: an existing binding can still be updated (keep -> 5)");
    CHECK(cellv("[ c ]") == 1 && cellv("[ c ]") == 2, "G5: an existing closure still runs with its own state (1, 2)");
    {
        /* the next new binding fails before ANY write: parse first, then
         * snapshot, then run */
        int err = 0;
        cell b = r0_s1_parse("[ brand-new: 1 ]", &err);
        take_snapshot();
        cell count = M[global_p + CTX_COUNT];
        r0_s1_run_persistent(b);
        CHECK(full_halt() && M[global_p + CTX_COUNT] == count && unchanged_since_snapshot(),
              "G2: a new binding into the full context fails before any write (context and loader heap byte-identical)");
        int ok = 1;
        for (int k = 0; k < 100; k++) {
            char src[48];
            snprintf(src, sizeof src, "[ another-new-%d: %d ]", k, k);
            b = r0_s1_parse(src, &err);
            if (err) { ok = 0; break; }
            take_snapshot();
            r0_s1_run_persistent(b);
            if (!full_halt() || !unchanged_since_snapshot()) ok = 0;
        }
        CHECK(ok && M[global_p + CTX_COUNT] == cap,
              "G6: 100 more rejected additions each fail with no mutation (count stays 256)");
        CHECK(cellv("[ judge [ never-bound: 1 ]  0 ]") == HALTED && full_halt(),
              "G7: judge cannot catch it: the fail-stop is not a SIN!");
        CHECK(cellv("[ kf c ]") == 6, "G8: the session keeps working after all that (kf c -> 6)");
    }

    /* ---- function (local) context: capacity 16 ---------------------------- */
    fresh();
    global_p = 0;
    CHECK(cellv("[ x: 7  mk: func [n] [ func [] [ n: + n 1 ] ]  c: mk 0  0 ]") == 0,
          "L0: caller state defined (x, counter closure c)");
    CHECK(cellv("[ f16: func [] [ a1: 1 a2: 2 a3: 3 a4: 4 a5: 5 a6: 6 a7: 7 a8: 8"
                " a9: 9 a10: 10 a11: 11 a12: 12 a13: 13 a14: 14 a15: 15 a16: 16  + a1 a16 ]  f16 ]") == 17,
          "L1: a function with exactly 16 locals fills its context and runs (17)");
    CHECK(cellv("[ f17: func [] [ a1: 1 a2: 2 a3: 3 a4: 4 a5: 5 a6: 6 a7: 7 a8: 8"
                " a9: 9 a10: 10 a11: 11 a12: 12 a13: 13 a14: 14 a15: 15 a16: 16 a17: 17  a1 ]  f17 ]") == HALTED
          && full_halt(),
          "L2: a 17th local fails as CONTEXT_FULL (before writing past the 16-slot context)");
    CHECK(cellv("[ x ]") == 7 && cellv("[ c ]") == 1,
          "L3: the caller's state is intact after the local overflow (x 7, c -> 1)");
    CHECK(cellv("[ p17: func [a b c d e f g h i j k l m n o p q] [ a ]"
                "  p17 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 ]") == HALTED && full_halt(),
          "L4: binding a 17th parameter fails as CONTEXT_FULL");
    CHECK(cellv("[ w: func [y] [ z: + y 1  either > y 0 [ f17 ] [ z ] ]  w 0 ]") == 1,
          "L5: a caller with its own locals still works (w 0 -> 1)");
    CHECK(cellv("[ w 1 ]") == HALTED && full_halt() && cellv("[ w 5 ]") == HALTED,
          "L6: an overflow inside a nested call fails the same way each time");
    CHECK(cellv("[ + x w 0 ]") == 8 && cellv("[ c ]") == 2,
          "L7: after nested overflows, caller state and closures are intact (8, c -> 2)");

    if (failures == 0) printf("all context capacity tests passed\n");
    return failures;
}
