/* gc_worklist_overflow_test.c - the GC mark worklist bound, exercised.
 *
 * A SEPARATE TEST BUILD (`make gc-worklist-overflow-test`), never a shipped
 * configuration. It is compiled with -DR0S1_GC_OVERFLOW_TEST_BUILD, the one
 * explicitly identified test switch: r0_s1.h then grows the managed heap from
 * 7,232 to 11,232 cells (room for 351 objects) while the mark worklist keeps
 * its 256 entries. The build-time worklist assertion would (correctly) reject
 * that layout, so this build alone skips it, in order to show what the
 * runtime bound does when the worklist does fill:
 *   - a valid program with more than 256 live objects in one block ends in a
 *     controlled resource fail-stop (R0S1_HALT_GC_WORKLIST_FULL; session
 *     outcome RESOURCE_ERROR / gc_worklist_full), not a SIN!, so judge does
 *     not catch it (machine-integrity failures are not judgeable);
 *   - nothing beyond the worklist is written: GC_A1/GC_A2 (the pending
 *     allocation), the unused gap and G1_OUT keep their values;
 *   - no mark bit is left set, so the abandoned collection leaves the heap as
 *     it was, and the persistent session keeps working: the same objects can
 *     be dropped and collected afterwards.
 * Without the runtime bound, the same programs overwrote GC_A1/GC_A2 and
 * fail-stopped with a spurious out-of-memory (GLON-ARCHITECTURE-MEMORY.md). */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

#ifndef R0S1_GC_OVERFLOW_TEST_BUILD
#error "build this test with make gc-worklist-overflow-test (R0S1_GC_OVERFLOW_TEST_BUILD)"
#endif
_Static_assert(GC_HEAP_LIMIT - GC_HEAP_BASE == 11232, "the enlarged-heap test build has an 11,232-cell managed heap");

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

#define GAP_LO   (GC_A2 + 1)
#define GAP_HI   (G1_OUT + 15)
#define CANARY   ((cell)0x5A5A5A50)
static void plant(void) { for (cell a = GAP_LO; a <= GAP_HI; a++) M[a] = CANARY; }
static int canaries_intact(void) {
    for (cell a = GAP_LO; a <= GAP_HI; a++) if (M[a] != CANARY) return 0;
    return 1;
}
static int alloc_state_sane(void) {
    return M[GC_A1] >= GC_HDR_STRIDE && M[GC_A1] <= (GC_HEAP_LIMIT - GC_HEAP_BASE)
        && M[GC_A2] >= 0 && M[GC_A2] < 8;
}
static int stray_marks(void) {
    int marked = 0;
    cell a = GC_HEAP_BASE;
    while (a < M[REG_HP] && a < GC_HEAP_LIMIT) {
        cell sz = M[a], fl = M[a + GC_HDR_FLAGS];
        if (sz <= 0) return -1;
        if (fl % 2 && (fl / 2) % 2) marked++;
        a += sz;
    }
    return marked;
}
static int returned_block_of(const r0_s1_outcome *oc, int n) {
    if (oc->status != R0S1_OUT_OK || oc->count != 1) return 0;
    cell v = r0_s1_result(0, 1);
    return (v & 15) == T_BLOCK && M[v - T_BLOCK] == n;
}

static char prog[8192];
/* N fresh objects kept live in one block, then garbage churn that forces
 * collections while all of them are reachable; the block is returned */
static const char *wide(const char *maker, int n, int judged) {
    int k = snprintf(prog, sizeof prog, "f: func [] [ %s ]\n"
                     "g: func [n] [ either = n 0 [ 0 ] [ reduce [ 1 2 3 ] g - n 1 ] ]\n%s",
                     maker, judged ? "judge [ " : "");
    k += snprintf(prog + k, sizeof prog - k, "b: reduce [");
    for (int i = 0; i < n; i++) k += snprintf(prog + k, sizeof prog - k, " f");
    k += snprintf(prog + k, sizeof prog - k, " ]\n");
    for (int i = 0; i < 40; i++) k += snprintf(prog + k, sizeof prog - k, "g 20\n");
    snprintf(prog + k, sizeof prog - k, "%sb\n", judged ? "] " : "");
    return prog;
}
static void run(const char *src, r0_s1_outcome *oc) { r0_s1_session_run(src, (unsigned)strlen(src), oc); }

int main(void) {
    printf("GC worklist overflow (enlarged-heap TEST build: %ld-cell managed heap, %d-entry worklist)\n",
           (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), (int)GC_WORKLIST_CAP);
    char m[220];
    r0_s1_outcome oc;

    CHECK((GC_HEAP_LIMIT - GC_HEAP_BASE) / GC_MIN_EXTENT > GC_WORKLIST_CAP,
          "O1: this test heap holds more objects (351) than the worklist has entries (256)");

    /* three small-object kinds (32 cells each). Closures are not used here:
     * made inside f, each also promotes a context (~112 cells per item), so the
     * test heap runs out of memory before 256 objects are live. */
    static const struct { const char *name, *maker; } kinds[] = {
        { "blocks", "reduce [ 1 ]" }, { "empty blocks", "reduce [ ]" }, { "SIN!s", "create-sin 'a 'b 1" },
    };
    for (int kd = 0; kd < 3; kd++) {
        /* below the bound: an ordinary, successful collection */
        fresh();
        plant();
        run(wide(kinds[kd].maker, 200, 0), &oc);
        snprintf(m, sizeof m, "O2 %s: 200 live objects collect and the block comes back intact", kinds[kd].name);
        CHECK(returned_block_of(&oc, 200) && M[GC_COLLECT_CNT] >= 2 && canaries_intact() && stray_marks() == 0, m);

        /* above the bound: a controlled resource fail-stop */
        fresh();
        plant();
        cell a1 = 0, a2 = 0;
        run(wide(kinds[kd].maker, 300, 0), &oc);
        a1 = M[GC_A1]; a2 = M[GC_A2];
        snprintf(m, sizeof m, "O3 %s: 300 live objects end in RESOURCE_ERROR / gc_worklist_full (halt reason %d)",
                 kinds[kd].name, r0_s1_halt_reason());
        CHECK(oc.status == R0S1_OUT_RESOURCE_ERROR && oc.detail == R0S1_DETAIL_GC_WORKLIST_FULL
              && r0_s1_halt_reason() == R0S1_HALT_GC_WORKLIST_FULL
              && strcmp(r0_s1_outcome_detail_name(oc.detail), "gc_worklist_full") == 0, m);
        snprintf(m, sizeof m, "O4 %s: nothing past the worklist was written (GC_A1=%ld GC_A2=%ld, gap and G1_OUT canaries)",
                 kinds[kd].name, (long)a1, (long)a2);
        CHECK(alloc_state_sane() && canaries_intact(), m);
        snprintf(m, sizeof m, "O5 %s: the abandoned collection left no mark bit set", kinds[kd].name);
        CHECK(stray_marks() == 0, m);

        /* the session survives: drop the objects, collect, build a fitting block */
        run("b: none\nf: none\nk: func [n] [ either = n 0 [ 0 ] [ reduce [ 1 2 3 ] k - n 1 ] ]\n"
            "k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30 k 30\n"
            "1\n", &oc);
        int after_ok = oc.status == R0S1_OUT_OK;
        run(wide(kinds[kd].maker, 200, 0), &oc);
        snprintf(m, sizeof m, "O6 %s: the same session then collects and builds 200 objects correctly", kinds[kd].name);
        CHECK(after_ok && returned_block_of(&oc, 200) && stray_marks() == 0 && canaries_intact(), m);
    }

    /* not a SIN!: judge does not catch a machine-resource fail-stop */
    fresh();
    run(wide("reduce [ 1 ]", 300, 1), &oc);
    cell t;
    CHECK(oc.status == R0S1_OUT_RESOURCE_ERROR && oc.detail == R0S1_DETAIL_GC_WORKLIST_FULL
          && !r0_s1_uncaught_error(&t, NULL, NULL),
          "O7: judge [ ... ] does not catch it: a resource fail-stop, not a SIN!");

    if (failures == 0) { printf("gc worklist overflow test: all passed\n"); return 0; }
    printf("gc worklist overflow test: %d failed\n", failures);
    return 1;
}
