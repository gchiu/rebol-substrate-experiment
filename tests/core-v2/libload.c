/* tests/core-v2/libload.c - GLON-VIEW-CORE-V2 library-load runner.
 *
 *   libload [-q] FILE...
 *
 * Loads each FILE, in order, into ONE fresh persistent session with no
 * default libraries (unlike the glon CLI), as the hosts do: ";;" comments
 * stripped, parsed, run.  For each file it prints one line:
 *   LOAD <file> ok|REFUSED|FAIL globals=<n> sites=<n> code_free=<n> loader=<n>
 * and, for the last file, its results as molded values ("RESULT ...").
 * A refusal prints the loader's message (stderr).  Exit: 0 when every file
 * loaded, 1 otherwise.  -q prints only failures and the result. */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* weak: the same runner also builds against a runtime without SITE-1 (C2) */
__attribute__((weak)) void r0_s1_site_usage(cell *count, cell *cap);

static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (!s || fread(s, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(s); return NULL; }
    s[n] = 0; fclose(f); return s;
}
static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
}
int main(int argc, char **argv) {
    int quiet = 0, a = 1;
    if (a < argc && !strcmp(argv[a], "-q")) { quiet = 1; a++; }
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    int status = 0;
    for (; a < argc; a++) {
        char *src = slurp(argv[a]);
        if (!src) { printf("LOAD %s FAIL (cannot read)\n", argv[a]); return 1; }
        strip_comments(src);
        int perr = 0; cell prog = r0_s1_parse(src, &perr); free(src);
        r0_s1_outcome oc;
        if (perr) r0_s1_session_parse_error(&oc); else r0_s1_session_run_block(prog, &oc);
        int ok = oc.status == R0S1_OUT_OK;
        cell g, gc, s, sc;
        r0_s1_global_usage(&g, &gc); s = -1; sc = -1; if (r0_s1_site_usage) r0_s1_site_usage(&s, &sc);
        long code_free = (long)R0S1_CODE_LIMIT - (long)(M[RV_DSLIM] - R0S1_DS_GUARD);
        if (!ok || !quiet)
            printf("LOAD %s %s globals=%ld sites=%ld code_free=%ld loader=%ld\n", argv[a],
                   ok ? "ok" : (perr ? "REFUSED" : "FAIL"), (long)g, (long)s, code_free,
                   (long)(M[GC_LOADER_HP] - R0S1_LOAD_BASE));
        if (!ok) { status = 1; break; }
        if (a == argc - 1 && oc.count > 0) {
            char buf[512];
            printf("RESULT");
            for (int k = 0; k < oc.count; k++) { r0_s1_mold(r0_s1_result(k, oc.count), buf, sizeof buf); printf(" %s", buf); }
            printf("\n");
        }
    }
    return status;
}
