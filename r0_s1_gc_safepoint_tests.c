/* r0_s1_gc_safepoint_tests.c - GC-safepoint invariant during closure
 * construction.
 *
 * Regression for a collector bug: emit_mkclosure used to allocate the closure
 * BEFORE popping the raw site-id/bias off the Glon data stack. When that
 * allocation triggered a collection, the collector scanned the DS assuming
 * every cell is a valid tagged Glon value. A raw site-id with a pointer tag
 * (e.g. site-id 6 -> tag T_BLOCK) was then misread as a heap pointer and the
 * machine fail-stopped ("out-of-range T_BLOCK").
 *
 * The fix pops the raw site-id/bias into evaluator registers before any
 * allocation, so at every GC safepoint the DS holds only valid tagged Glon
 * values.
 *
 * This test fills the managed heap to the limit with live strings, orphans
 * them, then constructs a closure whose func-site-id is 6 (a pointer-tagged
 * value) so that its allocation MUST trigger a collection exactly while the
 * site-id would previously have been on the DS. It then checks the collection
 * ran, the machine halted cleanly, and the closure semantics are correct.
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

static void eval(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { N = -1; return; }
    N = r0_s1_run_persistent(b);
}

static int got(int i) { return (i < N) ? (int)int_val(r0_s1_result(i, N)) : 0; }

int run_r0_s1_gc_safepoint_tests(void) {
    r0_s1_init();

    printf("R0-S1 GC-safepoint (closure construction)\n");

    /* fill-heap: a RAW fragment that appends managed strings to a loader block
     * until the managed frontier reaches the limit. The strings are anchored in
     * the block (live), so the fill reaches the limit without collecting. */
    eval("[ fill-heap: raw 1 [ "
         "    LIT SCRATCH_B ! LIT 0 LIT SCRATCH_A ! "
         "  Lfill: LIT REG_HP @ LIT 39984 LT ZBRANCH Ldone "
         "    LIT 16 LIT GC_KIND_STRING CALL alloc LIT T_STRING ADD "
         "    LIT SCRATCH_B @ LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_A @ ADD ! "
         "    LIT SCRATCH_A @ LIT 1 ADD LIT SCRATCH_A ! BRANCH Lfill "
         "  Ldone: ARITY 0 EXIT ] ]");
    CHECK(N == 1 && r0_s1_ran_cleanly(), "1: fill-heap raw loads cleanly");

    /* Five dummy funcs advance the func-site-id counter so the NEXT func is
     * site-id 6 (raw 6 -> tag T_BLOCK when misread as a value). They also pin a
     * little live state. Then fill-heap fills the managed heap to the limit and
     * the block is rebound, orphaning the strings (now dead). */
    eval("[ d1: func [] [ 1 ] d2: func [] [ 2 ] d3: func [] [ 3 ] "
         "  d4: func [] [ 4 ] d5: func [] [ 5 ] "
         "  buf: [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
         "         0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
         "         0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
         "         0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
         "         0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
         "         0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
         "         0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
         "         0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ] "
         "  fill-heap buf "
         "  buf: [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ] ]");
    CHECK(N == 1 && r0_s1_ran_cleanly(), "2: heap filled to the limit and strings orphaned");

    long gc_before = (long)r0_s1_gc_count();
    long hp_before = (long)r0_s1_heap_high();
    CHECK(hp_before >= 39984, "3: managed frontier is at the limit before the closure");

    /* This func is site-id 6. Its closure allocation must trigger a collection
     * (the heap is full and only the dead strings are reclaimable). Before the
     * fix, the collector misread the raw site-id 6 as a T_BLOCK and fail-stopped. */
    eval("[ f: func [] [ 42 ]  f ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "4: closure construction survives GC (42)");
    CHECK(r0_s1_gc_count() > gc_before, "5: a collection ran during closure construction");

    /* Independent captures and nested closures still work after a GC inside a
     * closure construction. (These use the raw `func` keyword directly rather
     * than the higher-order `does` vocabulary so the test stays self-contained.) */
    eval("[ mk: func [n] [ func [] [ n: + n 1 n ] ]  a: mk 0  b: mk 100  values [ a a b a ] ]");
    CHECK(N == 4 && got(0) == 1 && got(1) == 2 && got(2) == 101 && got(3) == 3,
          "6: independent captures survive (1 2 101 3)");

    eval("[ mk: func [n] [ func [] [ func [] [ n ] ] ]  f: mk 77  g: f  g ]");
    CHECK(N == 1 && got(0) == 77, "7: nested closure capture through two layers (77)");

    eval("[ base: 10  f: func [] [ base ]  base: 20  f ]");
    CHECK(N == 1 && got(0) == 20, "8: closure body resolves globals dynamically (20)");

    if (failures == 0) printf("all GC-safepoint tests passed\n");
    return failures;
}
