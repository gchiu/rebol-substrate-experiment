/* r0_s1_gc_interior_tests.c - the return-stack closure scan must accept only
 * real object starts.
 *
 * Regression for a collector defect found by experiments/glon-deflate (see
 * GLON-ARCHITECTURE-MEMORY.md, "Collector defect"). The return stack holds
 * frames, stack-local contexts and saved closure pointers, so the collector
 * scans it conservatively: a cell value p inside the managed heap, 16-aligned,
 * was taken for a closure when the cell at p - 15 (its would-be header flags)
 * carried the closure kind. Two facts made that unsound:
 *   - a STRING! stores one byte per cell, so inside a string any byte of 8-11
 *     (40-43, ...) passes the kind test;
 *   - an INTEGER! n is the cell n * 16, always 16-aligned, so an integer held
 *     in a stack-local context "points" into the heap when 2048 <= n < 2500.
 * The collector then traced the string's bytes as a closure and mark_push wrote
 * its mark bit into one of them (9 became 11): an immutable string changed.
 * Neighbouring bytes could instead fail-stop the mark phase.
 *
 * The fix accepts p only if a walk of the object sizes from GC_HEAP_BASE lands
 * exactly on p - 16. These tests build the exact situation with one integer.
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

static long got(int i) { return (i < N) ? (long)int_val(r0_s1_result(i, N)) : -1; }

int run_r0_s1_gc_interior_tests(void) {
    r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    printf("R0-S1 GC: return-stack closure scan accepts only object starts\n");

    /* helpers (raw is the RAW/masm trapdoor; the tests need addresses):
     *   nines n    a managed STRING! of n bytes, every byte 9
     *   addr s     the INTEGER! whose cell equals s's payload address
     *   byte s i   the byte at index i, read directly
     *   gc x       a full collection; answers 0 */
    eval("[ nines: raw 1 [ "
         "    LIT 16 DIV LIT SCRATCH_F ! "
         "    LIT SCRATCH_F @ LIT 16 ADD LIT 16 DIV LIT 16 MUL LIT GC_KIND_STRING CALL alloc LIT SCRATCH_C ! "
         "    LIT SCRATCH_F @ LIT SCRATCH_C @ ! LIT 0 LIT SCRATCH_A ! "
         "  Ln: LIT SCRATCH_A @ LIT SCRATCH_F @ LT ZBRANCH Ld "
         "    LIT 9 LIT SCRATCH_C @ LIT 1 ADD LIT SCRATCH_A @ ADD ! "
         "    LIT SCRATCH_A @ LIT 1 ADD LIT SCRATCH_A ! BRANCH Ln "
         "  Ld: LIT SCRATCH_C @ LIT T_STRING ADD ARITY 1 EXIT ] "
         "  addr: raw 1 [ LIT T_STRING SUB ARITY 1 EXIT ] "
         "  byte: raw 2 [ LIT 16 DIV LIT SCRATCH_A ! LIT T_STRING SUB LIT 1 ADD LIT SCRATCH_A @ ADD @ LIT 16 MUL ARITY 1 EXIT ] "
         "  gc: raw 1 [ DROP CALL collect LIT 0 ARITY 1 EXIT ] ]");
    CHECK(N == 1 && r0_s1_ran_cleanly(), "1: raw helpers load");

    /* 2. One integer in a live stack context equals the address one granule
     * inside a string of 9s: the would-be flags are byte 0 (9 = allocated +
     * closure kind) and the would-be fields are bytes 15-19 (also 9). */
    eval("[ s: nines 40 "
         "  probe: func [n] [ gc 0  byte s 0 ] "
         "  probe + addr s 1 ]");
    CHECK(N >= 1 && r0_s1_ran_cleanly(), "2: a collection with an interior integer on the stack completes");
    CHECK(r0_s1_ran_cleanly() && got(N - 1) == 9, "2: the string's byte 0 is still 9 (was changed to 11 by the mark bit)");
    eval("[ byte s 0 ]");
    CHECK(N == 1 && got(0) == 9, "2: the string is unchanged after the call returns");

    /* 3. The same at every interior granule of a longer string. */
    eval("[ t: nines 300 "
         "  walk: func [k] [ either = k 0 [ 0 ] [ probe2 + addr t k  walk - k 1 ] ] "
         "  probe2: func [n] [ gc 0 ] "
         "  walk 17 "
         "  count9: func [a b] [ either = b + a 1 [ either = byte t a 9 [ 0 ] [ 1 ] ] [ "
         "      + count9 a / + a b 2 count9 / + a b 2 b ] ] "
         "  count9 0 300 ]");
    CHECK(N >= 1 && r0_s1_ran_cleanly(), "3: collections with each interior granule on the stack complete");
    CHECK(r0_s1_ran_cleanly() && got(N - 1) == 0, "3: no byte of the 300-byte string changed");

    /* 4. A closure whose only references are on the return stack (it is being
     * invoked and was never bound) survives a collection inside it, and its
     * captured context stays intact. */
    eval("[ adder: func [a] [ func [b] [ gc 0  + a b ] ] "
         "  invoke adder 30 12 ]");
    CHECK(N >= 1 && r0_s1_ran_cleanly() && got(N - 1) == 42,
          "4: a closure referenced only from the stack survives a collection (30 + 12)");

    /* 5. Nested invocations: every active closure is still found. */
    eval("[ mk: func [x] [ func [y] [ gc 0  either = y 0 [ x ] [ + x invoke mk + x 1 - y 1 ] ] ] "
         "  invoke mk 1 5 ]");
    CHECK(N >= 1 && r0_s1_ran_cleanly() && got(N - 1) == 21,
          "5: six nested anonymous closures survive collections (1+2+...+6)");

    printf("  %d failure(s)\n", failures);
    return failures;
}
