/* scratch: run Glon files like ./glon, then dump per-cell attribution */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern unsigned char meas_kind[];
extern long meas_exec[];
extern long meas_var_exec;
extern FILE *meas_trace;
extern void *meas_owner[];
extern void *meas_chain[][6];
static char src[1 << 20];
static int load(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); return -3; }
    size_t n = fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
    char *r = src, *w = src;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; } *w = 0;
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "parse error in %s\n", path); return -1; }
    r0_s1_outcome oc; r0_s1_session_run_block(b, &oc);
    if (oc.status) fprintf(stderr, "%s: outcome %s\n", path, r0_s1_outcome_status_name(oc.status));
    return oc.status;
}
int main(int argc, char **argv) {
    /* argv[1] = dump file, argv[2] = "-e" expr | files... ; libraries first */
    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    cell rt_end = asm_here();
    /* Run from the repository root: the ordinary core libraries are loaded
     * before the workload counter is reset. */
    load("jupyter/prelude.glon"); load("demo/shop/strings.glon");
    memset(meas_exec, 0, sizeof(long) * 65536);        /* count only the workload */
    meas_var_exec = 0;
    for (int a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "-t")) { meas_trace = fopen(argv[++a], "w"); continue; }
        if (!strcmp(argv[a], "-e")) { a++; r0_s1_outcome oc; r0_s1_session_run(argv[a], (unsigned)strlen(argv[a]), &oc);
            if (oc.status == 0) { char out[256]; r0_s1_mold(r0_s1_result(oc.count - 1, oc.count), out, sizeof out); fprintf(stderr, "result %s\n", out); }
            else fprintf(stderr, "outcome %d\n", oc.status); continue; }
        load(argv[a]);
    }
    FILE *o = fopen(argv[1], "w");
    fprintf(o, "RTEND %ld HERE %ld VAR %ld\n", (long)rt_end, (long)asm_here(), meas_var_exec);
    for (cell c = 0; c < 65536; c++)
        if (meas_exec[c] || (c >= 256 && c < asm_here()))
        { fprintf(o, "%ld %d %ld", (long)c, (int)meas_kind[c], meas_exec[c]);
          for (int d = 0; d < 6 && meas_chain[c][d]; d++) fprintf(o, " %p", meas_chain[c][d]);
          fprintf(o, "\n"); }
    fclose(o);
    return 0;
}
