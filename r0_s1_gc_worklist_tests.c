/* r0_s1_gc_worklist_tests.c - the GC mark worklist is bounded (64K configuration).
 *
 * The collector's mark worklist has GC_WORKLIST_CAP (256) entries. Each managed
 * object is pushed at most once per collection, only managed objects are
 * pushed, and every managed object occupies at least GC_MIN_EXTENT (32) cells,
 * so the 7,232-cell managed heap of the 64K map bounds the worklist at 226
 * entries. r0_s1.h asserts that bound at build time, and the runtime bounds
 * every push anyway (R0S1_HALT_GC_WORKLIST_FULL). This file checks the 64K
 * side: the arithmetic, the 32-cell minimum on real heaps, and that the
 * widest valid programs collect without ever reaching the bound or writing
 * past the worklist. The overflow side runs in a separate enlarged-heap build
 * (gc_worklist_overflow_test.c; `make gc-worklist-overflow-test`).
 * See GLON-ARCHITECTURE-MEMORY.md, "GC worklist". */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

#ifdef R0S1_GC_OVERFLOW_TEST_BUILD
#error "the 64K suite must not be built with the enlarged-heap test switch"
#endif

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

/* canaries: the unused cells between GC_A2 and G1_OUT, and the head of G1_OUT */
#define GAP_LO   (GC_A2 + 1)
#define GAP_HI   (G1_OUT + 15)
#define CANARY   ((cell)0x5A5A5A50)
static cell saved[GAP_HI - GAP_LO + 1];
static void plant(void) { for (cell a = GAP_LO; a <= GAP_HI; a++) { saved[a - GAP_LO] = M[a]; M[a] = CANARY; } }
static void unplant(void) { for (cell a = GAP_LO; a <= GAP_HI; a++) M[a] = saved[a - GAP_LO]; }
static int canaries_intact(void) {
    for (cell a = GAP_LO; a <= GAP_HI; a++) if (M[a] != CANARY) return 0;
    return 1;
}
/* GC_A1/GC_A2 hold the pending allocation's extent and kind: small, sane values */
static int alloc_state_sane(void) {
    return M[GC_A1] >= GC_HDR_STRIDE && M[GC_A1] <= (GC_HEAP_LIMIT - GC_HEAP_BASE)
        && M[GC_A2] >= 0 && M[GC_A2] < 8;
}
/* walk the managed heap; report the smallest allocated extent and any stray mark bit */
static void heap_walk(long *min_extent, long *allocated, int *marked, int *bad) {
    *min_extent = 1L << 30; *allocated = 0; *marked = 0; *bad = 0;
    cell a = GC_HEAP_BASE;
    while (a < M[REG_HP] && a < GC_HEAP_LIMIT) {
        cell sz = M[a], fl = M[a + GC_HDR_FLAGS];
        if (sz <= 0) { *bad = 1; return; }
        if (fl % 2) { (*allocated)++; if (sz < *min_extent) *min_extent = (long)sz; if ((fl / 2) % 2) (*marked)++; }
        a += sz;
    }
}

/* the block a run returned: n elements, each a live allocated managed object */
static int block_ok(const r0_s1_outcome *oc, int n) {
    if (oc->status != R0S1_OUT_OK || oc->count != 1) return 0;
    cell v = r0_s1_result(0, 1);
    if ((v & 15) != T_BLOCK) return 0;
    cell p = v - T_BLOCK;
    if (M[p] != n) return 0;
    for (int i = 0; i < n; i++) {
        cell e = M[p + 2 + i], q = e & ~(cell)15;
        if (q < GC_HEAP_BASE + GC_HDR_STRIDE || q >= GC_HEAP_LIMIT) return 0;
        if (M[q - GC_HDR_STRIDE + GC_HDR_FLAGS] % 2 != 1) return 0;   /* allocated */
    }
    return 1;
}

/* a valid program: N fresh objects of one kind kept live in one block, then
 * garbage churn that forces collections while they are live */
static char prog[8192];
static const char *make(const char *maker, int n, int churn) {
    int k = snprintf(prog, sizeof prog, "f: func [] [ %s ]\nb: reduce [", maker);
    for (int i = 0; i < n; i++) k += snprintf(prog + k, sizeof prog - k, " f");
    k += snprintf(prog + k, sizeof prog - k,
                  " ]\ng: func [n] [ either = n 0 [ 0 ] [ reduce [ 1 2 3 ] g - n 1 ] ]\n");
    for (int i = 0; i < churn; i++) k += snprintf(prog + k, sizeof prog - k, "g 20\n");
    snprintf(prog + k, sizeof prog - k, "b\n");
    return prog;
}

int run_r0_s1_gc_worklist_tests(void) {
    printf("R0/S1 GC worklist (64K configuration)\n");
    char m[200];

    /* ---- the bound, as built ---- */
    long heap = GC_HEAP_LIMIT - GC_HEAP_BASE, bound = heap / GC_MIN_EXTENT;
    CHECK(GC_WORKLIST_CAP == 256 && GC_WORKLIST + GC_WORKLIST_CAP == GC_A1,
          "W1: the worklist is the 256 cells [GC_WORKLIST, GC_A1)");
    CHECK(heap == 7232 && GC_MIN_EXTENT == 32 && bound == 226,
          "W2: the 64K managed heap (7,232 cells) holds at most 226 objects of >= 32 cells");
    CHECK(bound <= GC_WORKLIST_CAP, "W3: so the worklist bound (226) is within its capacity (256)");

    /* ---- the 32-cell minimum holds on real heaps ---- */
    fresh();
    r0_s1_outcome oc;
    const char *mixed =
        "s: func [] [ create-sin 'a 'b 1 ]\n"
        "c: func [] [ func [] [ 1 ] ]\n"
        "k: func [] [ reduce [ 1 ] ]\n"
        "e: func [] [ reduce [ ] ]\n"
        "x: reduce [ s c k e s c k e s c k e s c k e ]\n"
        "x\n";
    r0_s1_session_run(mixed, (unsigned)strlen(mixed), &oc);
    long minx, nalloc; int nmarked, bad;
    heap_walk(&minx, &nalloc, &nmarked, &bad);
    snprintf(m, sizeof m, "W4: blocks, empty blocks, closures, SIN!s: smallest managed object %ld cells (>= %d) over %ld objects",
             minx, GC_MIN_EXTENT, nalloc);
    CHECK(oc.status == R0S1_OUT_OK && !bad && minx >= GC_MIN_EXTENT && nalloc > 16, m);

    /* ---- the widest valid programs: never the bound, never a write past it ---- */
    /* fit: the largest count that fits the 7,232-cell heap. A closure made
     * inside f captures f's activation, so each one also promotes a context:
     * two objects and ~112 cells per item, against 32 for a block or SIN!. */
    static const struct { const char *name, *maker; int fit; } kinds[] = {
        { "blocks",   "reduce [ 1 ]",        160 },
        { "closures", "func [] [ 1 ]",        40 },
        { "SIN!s",    "create-sin 'a 'b 1",  160 },
    };
    for (int kd = 0; kd < 3; kd++) {
        int fit_ok = 1, never_full = 1, canary_ok = 1, alloc_ok = 1, marks_ok = 1, collected = 0;
        for (int n = 40; n <= 320; n += 40) {
            fresh();
            plant();
            cell gc0 = M[GC_COLLECT_CNT];
            const char *src = make(kinds[kd].maker, n, 25);
            r0_s1_session_run(src, (unsigned)strlen(src), &oc);
            if (M[GC_COLLECT_CNT] > gc0) collected++;
            if (oc.detail == R0S1_DETAIL_GC_WORKLIST_FULL || r0_s1_halt_reason() == R0S1_HALT_GC_WORKLIST_FULL) never_full = 0;
            if (!canaries_intact()) canary_ok = 0;
            if (!alloc_state_sane()) alloc_ok = 0;
            heap_walk(&minx, &nalloc, &nmarked, &bad);
            if (nmarked || bad) marks_ok = 0;
            /* up to 160 fresh objects (plus the block and the churn) fit the heap */
            if (n <= kinds[kd].fit && !block_ok(&oc, n)) fit_ok = 0;
            unplant();
        }
        snprintf(m, sizeof m, "W5 %s: 40..%d live items collect correctly (blocks intact)", kinds[kd].name, kinds[kd].fit);
        CHECK(fit_ok, m);
        snprintf(m, sizeof m, "W6 %s: 40..320 live items (to out-of-memory) never report a full worklist", kinds[kd].name);
        CHECK(never_full, m);
        snprintf(m, sizeof m, "W7 %s: nothing past the worklist is written (gap and G1_OUT canaries, GC_A1/GC_A2 sane)", kinds[kd].name);
        CHECK(canary_ok && alloc_ok, m);
        snprintf(m, sizeof m, "W8 %s: no mark bit survives a run (%d of 8 runs collected)", kinds[kd].name, collected);
        CHECK(marks_ok && collected >= 6, m);
    }

    /* ---- roots: many globals referencing distinct managed objects ---- */
    {
        fresh();
        plant();
        int k = 0;
        /* 140 bindings: 3 values each keeps the program under the 512-value block limit */
        for (int i = 0; i < 140; i++) k += snprintf(prog + k, sizeof prog - k, "v%d: reduce [ %d ]\n", i, i);
        k += snprintf(prog + k, sizeof prog - k,
                      "g: func [n] [ either = n 0 [ 0 ] [ reduce [ 1 2 3 ] g - n 1 ] ]\n");
        for (int i = 0; i < 25; i++) k += snprintf(prog + k, sizeof prog - k, "g 20\n");
        snprintf(prog + k, sizeof prog - k, "v139\n");
        cell gc0 = M[GC_COLLECT_CNT];
        r0_s1_session_run(prog, (unsigned)strlen(prog), &oc);
        cell v = oc.count == 1 ? r0_s1_result(0, 1) : 0;
        int one = (v & 15) == T_BLOCK && M[v - T_BLOCK] == 1 && M[v - T_BLOCK + 2] == mk_int(139);
        int ok = oc.status == R0S1_OUT_OK && one && M[GC_COLLECT_CNT] > gc0 && canaries_intact() && alloc_state_sane();
        unplant();
        CHECK(ok,
              "W9: 140 globals bound to distinct objects (all pushed as roots) collect correctly");
    }

    return failures;
}
