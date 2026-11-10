/* B1 baseline: the existing Glon evaluator and the existing hand-written
 * Forth-style masm fib, measured with the same instrumented S1 copy.
 * Uses the repository's production runtime unchanged.
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include "s1_count.h"

#include <stdio.h>
#include <string.h>

#define FIB_CALLS 8361L
#define MASM_BASE_BODY 15L   /* F's base branch: DUP LIT LT ZBRANCH EXIT */

static char src[1 << 20];

static int load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -3; }
    size_t n = fread(src, 1, sizeof src - 1, f);
    fclose(f); src[n] = 0;
    char *r = src, *w = src;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "parse error in %s\n", path); return -1; }
    r0_s1_outcome oc;
    r0_s1_session_run_block(b, &oc);
    return oc.status;
}

static const char *root = ".";

static void setup(void) {
    char path[4096];
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    snprintf(path, sizeof path, "%s/jupyter/prelude.glon", root);
    load(path);
    snprintf(path, sizeof path, "%s/demo/shop/strings.glon", root);
    load(path);
}

static int eval(const char *expr, long *value) {
    r0_s1_outcome oc;
    r0_s1_session_run(expr, (unsigned)strlen(expr), &oc);
    if (oc.status) return 0;
    if (value) *value = (oc.count > 0) ? int_val(r0_s1_result(oc.count - 1, oc.count)) : -1;
    return 1;
}

static const char *GLON_FIB_DEF =
    "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ]";
static const char *MASM_FIB =
    "fibm: masm 1 [ LIT 16 DIV CALL F LIT 16 MUL ARITY 1 EXIT  "
    "F: DUP LIT 2 LT ZBRANCH R EXIT  "
    "R: DUP LIT 1 SUB CALL F >R LIT 2 SUB CALL F R> ADD EXIT ]";

static void define_only(const char *src) {
    long ignored = -1;
    if (!eval(src, &ignored)) { fprintf(stderr, "definition failed\n"); }
}

static void run_case(const char *label, const char *def, const char *call, long *total_out) {
    setup();
    define_only(def);
    b1_reset_counts();
    long value = -1;
    int ok = eval(call, &value);
    long total = b1_insns;
    printf("%s\n", label);
    printf("  result: %ld%s\n", value, ok ? "" : " (failed)");
    printf("  executed S1 instructions: %ld\n", total);
    printf("  data-stack high-water: %ld cells\n", 16384 - b1_min_sp);
    printf("  return-stack high-water: %ld cells\n", 24576 - b1_min_rp);
    if (total_out) *total_out = total;
}

int main(int argc, char **argv) {
    if (argc > 1) root = argv[1];
    char buf[4096];
    long masm18 = 0, masm1 = 0;

    setup();
    printf("runtime emitted code: %ld cells\n", (long)r0_s1_code_size());
    {
        cell before = M[GC_LOADER_HP];
        define_only(GLON_FIB_DEF);
        printf("Glon fib source/loader cells: %ld\n", (long)(M[GC_LOADER_HP] - before));
    }

    run_case("Glon evaluator, fib 18 (definition loaded first)",
             GLON_FIB_DEF, "fib 18", NULL);

    setup(); define_only(MASM_FIB); b1_reset_counts();
    { long v = -1; int ok = eval("fibm 18", &v); masm18 = b1_insns;
      printf("masm fib 18\n  result: %ld%s\n  executed S1 instructions: %ld\n",
             v, ok ? "" : " (failed)", masm18);
      printf("  data-stack high-water: %ld cells\n", 16384 - b1_min_sp);
      printf("  return-stack high-water: %ld cells\n", 24576 - b1_min_rp); }

    setup(); define_only(MASM_FIB); b1_reset_counts();
    { long v = -1; int ok = eval("fibm 1", &v); masm1 = b1_insns;
      printf("masm fib 1 (one base activation, definition loaded first)\n  result: %ld%s\n  executed S1 instructions: %ld\n",
             v, ok ? "" : " (failed)", masm1); }

    printf("masm fib, naive recursion, %ld activations\n", FIB_CALLS);
    printf("  per activation (subtracting top-level/wrapper, %ld base body): %.2f\n",
           MASM_BASE_BODY, (double)(masm18 - (masm1 - MASM_BASE_BODY)) / (double)FIB_CALLS);

    setup();
    {
        cell before = asm_here();
        int err = 0;
        snprintf(buf, sizeof buf, "%s 0", MASM_FIB);
        r0_s1_parse(buf, &err);
        cell after = asm_here();
        printf("masm fib emitted code: %ld cells%s\n", (long)(after - before),
               err ? " (parse error)" : "");
    }

    return 0;
}
