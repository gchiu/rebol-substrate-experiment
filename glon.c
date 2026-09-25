/* glon.c - the native Glon command-line runner.
 *
 * A thin host around the existing persistent-session facade (r0_s1_session_run
 * / r0_s1_session_run_block): it initialises ONE runtime, loads the shipped
 * core libraries, then runs each FILE in that same session so definitions
 * persist across files. It adds no language semantics and no parser/evaluator
 * code; every outcome is classified by the runtime's own structured state.
 *
 * Usage:
 *   glon FILE...
 *
 * A FILE is ordinary Glon source (a top-level sequence, optionally wrapped in
 * one outer [ ... ]; `;;` line comments are allowed), exactly the source form
 * the native session host and the WASM glon_load accept.
 *
 * The core libraries are located beside the executable in glon-lib/:
 *   glon-lib/prelude.glon   the smallest ordinary core vocabulary
 *   glon-lib/strings.glon   immutable STRING! operations (s/+ s/= s/length s/print)
 * They are ordinary Glon source, parsed and run by the existing loader; they
 * are not compiled into the binary.
 *
 * Exit status: 0 all files clean; 1 a Glon parse/SIN!/resource/machine error;
 * 2 usage, IO or library failure.
 */

#define _GNU_SOURCE
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_SOURCE (1u << 20)

/* ---- source files -------------------------------------------------------- */

static char *read_source(const char *path, char *err, size_t errcap) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errcap, "cannot open"); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f); snprintf(err, errcap, "cannot seek"); return NULL;
    }
    long n = ftell(f);
    if (n < 0) { fclose(f); snprintf(err, errcap, "cannot measure"); return NULL; }
    if ((unsigned long)n > MAX_SOURCE) {
        fclose(f);
        snprintf(err, errcap, "source is larger than %u bytes", (unsigned)MAX_SOURCE);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); snprintf(err, errcap, "out of memory"); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); snprintf(err, errcap, "cannot read"); return NULL; }
    buf[got] = 0;
    return buf;
}

/* strip `;;` line comments, exactly as the native host and WASM loader do */
static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

/* ---- library discovery --------------------------------------------------- */

static char exe_dir[2048];

static int find_exe_dir(void) {
    ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof exe_dir - 1);
    if (n <= 0) return -1;
    exe_dir[n] = 0;
    char *slash = strrchr(exe_dir, '/');
    if (slash) *slash = 0; else strcpy(exe_dir, ".");
    return 0;
}

static int load_library(const char *path) {
    char err[512];
    char *src = read_source(path, err, sizeof err);
    if (!src) { fprintf(stderr, "glon: %s: %s\n", path, err); return -1; }

    strip_comments(src);
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    free(src);

    r0_s1_outcome oc;
    if (perr) r0_s1_session_parse_error(&oc);
    else r0_s1_session_run_block(prog, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "glon: %s: library did not load\n", path);
        return -1;
    }
    return 0;
}

static int load_default_libraries(void) {
    if (find_exe_dir() != 0) {
        fprintf(stderr, "glon: cannot locate the executable directory\n");
        return -1;
    }
    static const char *names[] = { "prelude.glon", "strings.glon" };
    char path[4096];
    for (unsigned k = 0; k < sizeof names / sizeof names[0]; k++) {
        snprintf(path, sizeof path, "%s/glon-lib/%s", exe_dir, names[k]);
        if (load_library(path) != 0) return -1;
    }
    return 0;
}

/* ---- outcome reporting --------------------------------------------------- */

static void report_outcome(const r0_s1_outcome *out, const char *path) {
    switch (out->status) {
    case R0S1_OUT_OK:
        if (out->count <= 0) return;
        if (out->count == 1 && r0_s1_result(0, 1) == R0_NONE) return;
        {
            char buf[4096];
            for (int i = 0; i < out->count; i++) {
                if (i) putchar(' ');
                r0_s1_mold(r0_s1_result(i, out->count), buf, (int)sizeof buf);
                fputs(buf, stdout);
            }
            putchar('\n');
        }
        return;
    case R0S1_OUT_PARSE_ERROR:
        fprintf(stderr, "glon: %s: ** parse error", path);
        if (out->detail != R0S1_DETAIL_NONE)
            fprintf(stderr, ": %s", r0_s1_outcome_detail_name(out->detail));
        fputc('\n', stderr);
        return;
    case R0S1_OUT_RESOURCE_ERROR:
        fprintf(stderr, "glon: %s: ** resource error: %s\n",
                path, r0_s1_outcome_detail_name(out->detail));
        return;
    case R0S1_OUT_UNCAUGHT_SIN: {
        char t[1024], i[1024], a[1024];
        r0_s1_mold(out->sin_type, t, sizeof t);
        r0_s1_mold(out->sin_id, i, sizeof i);
        r0_s1_mold(out->sin_arg, a, sizeof a);
        fprintf(stderr, "glon: %s: ** uncaught #[SIN! %s %s %s]\n", path, t, i, a);
        return;
    }
    case R0S1_OUT_HALT:
        if (out->detail == R0S1_DETAIL_CONTEXT_FULL)
            fprintf(stderr, "glon: %s: ** halted: context full (no SIN!: a machine-level fail-stop)\n", path);
        else
            fprintf(stderr, "glon: %s: ** halted (no SIN!: a machine-level fail-stop)\n", path);
        return;
    default:
        fprintf(stderr, "glon: %s: ** unknown session outcome\n", path);
        return;
    }
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: glon FILE...\n");
        return 2;
    }

    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    if (load_default_libraries() != 0) return 2;

    for (int i = 1; i < argc; i++) {
        char err[512];
        char *src = read_source(argv[i], err, sizeof err);
        if (!src) { fprintf(stderr, "glon: %s: %s\n", argv[i], err); return 2; }

        strip_comments(src);
        int perr = 0;
        cell prog = r0_s1_parse(src, &perr);
        free(src);

        r0_s1_outcome oc;
        if (perr) r0_s1_session_parse_error(&oc);
        else r0_s1_session_run_block(prog, &oc);

        report_outcome(&oc, argv[i]);
        if (oc.status != R0S1_OUT_OK) return 1;
    }
    return 0;
}
