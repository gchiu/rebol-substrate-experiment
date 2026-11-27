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

    /* GLON-MEM-5: the closure check skips, before the object-start walk, a
     * candidate whose would-be flags already carry the mark bit.  These cases
     * would catch the check accepting what it must reject (6) or rejecting
     * what it must keep (7, 8).  Helpers: setb s i v writes byte i of s;
     * cflags c is the header flags word of closure c (9 = allocated closure,
     * unmarked); churn k allocates k throw-away 16-byte strings (48 cells, the
     * size of a small closure's block; k is small, as each frame keeps its
     * string live until it returns), so a closure freed by
     * mistake would be overwritten before it is used; bat b i is element i of
     * block b.  A word bound to a closure calls it, so closure values are
     * passed as get-words (:inner). */
    _Static_assert(T_CLOSURE == 8 && GC_HDR_STRIDE - GC_HDR_FLAGS == 15, "cflags below");
    eval("[ setb: raw 3 [ LIT 16 DIV LIT SCRATCH_B ! LIT 16 DIV LIT SCRATCH_A ! "
         "    LIT T_STRING SUB LIT 1 ADD LIT SCRATCH_A @ ADD LIT SCRATCH_C ! "
         "    LIT SCRATCH_B @ LIT SCRATCH_C @ ! LIT 0 ARITY 1 EXIT ] "
         "  cflags: raw 1 [ LIT 8 SUB LIT 15 SUB @ LIT 16 MUL ARITY 1 EXIT ] "   /* 8 = T_CLOSURE (not a masm name) */
         "  churn: func [k] [ either = k 0 [ 0 ] [ nines 16  churn - k 1 ] ] "
         "  bat: masm 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD @ ARITY 1 EXIT ] ]");
    CHECK(N == 1 && r0_s1_ran_cleanly(), "6: helpers load (setb, cflags, churn, bat)");

    /* 6. Fake candidates whose would-be flags read "closure, marked": byte 11
     * (allocated + mark + closure kind) and byte 10 (mark + closure kind).
     * They must not be accepted (nothing may be written into the string), and
     * the collection must complete. */
    eval("[ s6: nines 40  setb s6 0 11  setb s6 16 10 "
         "  probe6: func [n m] [ gc 0  + byte s6 0 byte s6 16 ] "
         "  probe6 + addr s6 1 + addr s6 17 ]");
    CHECK(N >= 1 && r0_s1_ran_cleanly() && got(N - 1) == 21,
          "6: fakes reading 'closure, marked' are rejected; the string is unchanged (11 + 10)");
    eval("[ count6: func [a b] [ either = b + a 1 [ either = byte s6 a 9 [ 0 ] [ 1 ] ] [ "
         "      + count6 a / + a b 2 count6 / + a b 2 b ] ] "
         "  count6 0 40 ]");
    CHECK(N >= 1 && got(N - 1) == 2, "6: only the two bytes set differ from 9 (no mark bit written)");

    /* 7. Closures reachable only through a block, through another closure's
     * captured context (nested), or through two references (shared), across
     * three collections with churn between them: they must all survive, still
     * read as allocated, unmarked closures, and still compute. */
    eval("[ mkc: func [x] [ func [y] [ + x y ] ] "
         "  wrap: func [g x] [ func [y] [ + invoke :g y x ] ] "
         "  held: reduce [ mkc 1 mkc 2 mkc 3 ] "
         "  inner: mkc 40 "
         "  outer: wrap :inner 500 "
         "  only: wrap mkc 7 1000 "
         "  alias: held "
         "  gc 0  churn 30  gc 0  churn 30  gc 0 "
         "  + + + + invoke bat held 0 10  invoke bat alias 2 10  invoke :outer 1  invoke :only 1 "
         "    cflags bat held 1 ]");
    CHECK(N >= 1 && r0_s1_ran_cleanly() && got(N - 1) == 11 + 13 + 541 + 1008 + 9,
          "7: block-held, nested, shared and captured closures survive three collections and compute (1582)");
    eval("[ + + cflags :inner cflags :outer cflags :only ]");
    CHECK(N == 1 && got(0) == 27, "7: their headers still read allocated + closure, mark clear (9 + 9 + 9)");

    /* 8. One closure referenced from many places at once (most references
     * then meet it already marked): it survives and computes. */
    eval("[ many: reduce [ :inner :inner :inner :inner :inner :inner :inner :inner ] "
         "  more: reduce [ many many :outer :outer ] "
         "  gc 0  churn 30  gc 0 "
         "  + invoke bat many 7 2  invoke bat more 3 1 ]");
    CHECK(N >= 1 && r0_s1_ran_cleanly() && got(N - 1) == 42 + 541,
          "8: a closure met already marked many times survives (42 + 541)");

    printf("  %d failure(s)\n", failures);
    return failures;
}
