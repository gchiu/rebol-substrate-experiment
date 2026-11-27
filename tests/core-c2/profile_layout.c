/* tests/core-c2/profile_layout.c - GLON-CORE-C2 per-profile layout probe.
 *
 * Built once per memory profile (make profile-check).  Prints the effective
 * layout (the same line for the constrained defaults and for the generated
 * constrained profile header, which profile-check compares), then checks that
 * the S1 object agrees with the runtime: after s1_reset, SP / RP / HP must be
 * the runtime's R0S1_DS_INIT / R0S1_RS_INIT / GC_HEAP_BASE, i.e. the generated
 * S1 copy and the profile header came from the same profile.  Finally it runs
 * a Glon expression through the session. */
#include "r0_s1.h"
#include "r0_s1_layout.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

int main(void) {
#ifdef GLON_PROFILE_NAME
    const char *name = GLON_PROFILE_NAME;
#else
    const char *name = "constrained";
#endif
    /* one line, compared verbatim between the default build and the
     * generated constrained header */
    printf("LAYOUT mem=%ld ds=%ld rs=%ld code_limit=%ld heap=[%ld,%ld) worklist=%ld+%ld "
           "loader=[%ld,%ld) arena=%ld tasks=%d x %d table=%ld globals=%ld symbols=%ld radix=%ld\n",
           (long)R0S1_MEM_CELLS, (long)R0S1_DS_INIT, (long)R0S1_RS_INIT, (long)R0S1_CODE_LIMIT,
           (long)GC_HEAP_BASE, (long)GC_HEAP_LIMIT, (long)GC_WORKLIST, (long)GC_WORKLIST_CAP,
           (long)R0S1_LOAD_BASE, (long)R0S1_HEAP_LIMIT, (long)M1_ARENA_BASE,
           (int)M1_MAX_TASKS, (int)M1_TASK_CELLS, (long)M1_TASK_TABLE,
           (long)R0S1_GLOBAL_CAP, (long)R0S1_SYM_CAP, (long)R0S1_HASH_RADIX);
    printf("profile-layout: %s\n", name);

    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    s1_reset();
    CHECK(M[REG_SP] == R0S1_DS_INIT, "S1 data-stack top == R0S1_DS_INIT (S1 copy and header agree)");
    CHECK(M[REG_RP] == R0S1_RS_INIT, "S1 return-stack top == R0S1_RS_INIT");
    CHECK(M[REG_HP] == GC_HEAP_BASE, "S1 heap base == GC_HEAP_BASE");
    CHECK(R0S1_RS_INIT - R0S1_DS_INIT >= R0S1_RS_MIN, "main return stack >= the 4096-cell minimum");

    cell count, cap;
    r0_s1_global_usage(&count, &cap);
    CHECK(cap == R0S1_GLOBAL_CAP, "global context has the profile capacity");

    const char *src = "f: func [n] [ either = n 0 [ 0 ] [ + n f - n 1 ] ] f 20";
    r0_s1_outcome oc;
    r0_s1_session_run(src, (unsigned)strlen(src), &oc);
    cell r = (oc.status == R0S1_OUT_OK && oc.count >= 1) ? r0_s1_result(oc.count - 1, oc.count) : -1;
    CHECK(r0_tag(r) == T_INT && int_val(r) == 210, "a recursive program evaluates (sum 1..20 = 210)");

    printf("profile-layout %s: %s (%d failure%s)\n", name, failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
