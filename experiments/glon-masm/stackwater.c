/* experiments/glon-masm/stackwater.c -- how much of the main data stack and
 * return stack real workloads use, by high-water mark: before each file runs,
 * every free cell of both stacks is set to a poison value; afterwards the
 * deepest cell that no longer holds it is the high-water mark. (The runtime's
 * r0_s1_sp_min / rp_min do not record the deepest point inside a run.)
 *   stackwater ROOT FILE...      files are loaded after prelude and strings
 * Prints, per file: where emitted code ends, the data stack's deepest use and
 * the room left between code and that depth, and the return stack's deepest use.
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POISON ((cell)0x5A5A5A5)
static char buf[1 << 20];
static int file(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(2); }
    size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    char *r = buf, *w = buf;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    int e = 0; cell b = r0_s1_parse(buf, &e); if (e) return -2;
    r0_s1_run_persistent(b); return r0_s1_ran_cleanly() ? 0 : -3;
}
int main(int argc, char **argv) {
    char p[4096];
    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    const char *libs[] = { "jupyter/prelude.glon", "demo/shop/strings.glon", NULL };
    for (int i = 0; libs[i]; i++) { snprintf(p, sizeof p, "%s/%s", argv[1], libs[i]); if (file(p)) return 1; }
    printf("%-50s %7s %9s %9s %9s\n", "file", "code end", "DS deepest", "DS room", "RS deepest");
    for (int a = 2; a < argc; a++) {
        cell lo = asm_here();
        for (cell c = lo; c < R0S1_DS_INIT; c++) M[c] = POISON;           /* free data stack (and future code) */
        for (cell c = R0S1_DS_INIT; c < R0S1_RS_INIT; c++) M[c] = POISON; /* return stack */
        int rc = file(argv[a]);
        cell code = asm_here(), ds = R0S1_DS_INIT, rs = R0S1_RS_INIT;
        for (cell c = code; c < R0S1_DS_INIT; c++) if (M[c] != POISON) { ds = c; break; }
        for (cell c = R0S1_DS_INIT; c < R0S1_RS_INIT; c++) if (M[c] != POISON) { rs = c; break; }
        printf("%-50s %7ld %9ld %9ld %9ld%s\n", argv[a], (long)code, (long)(R0S1_DS_INIT - ds),
               (long)(ds - code), (long)(R0S1_RS_INIT - rs), rc ? "  (run did not end cleanly)" : "");
    }
    return 0;
}
