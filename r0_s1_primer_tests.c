/* r0_s1_primer_tests.c - doc-tests for the Glon primer.
 *
 * demo/shop/primer.txt is the single source of truth for the primer: its
 * examples are rendered into GLON-PRIMER.md and demo/shop/primer.html by
 * demo/shop/build-primer.py (`--check` proves both are regenerated
 * byte-identically). This group reads the SAME manifest and runs every
 * example exactly as the live page does: a fresh runtime per example
 * (bootstrap + CASE, plus the shipped task library for `@env tasks`), the
 * example source run through r0_s1_show_run, whose text must equal the
 * example's `@expect` line. In an @expect line, `<site>` stands for exactly
 * one positive integer (an internal lexical site id, which changes whenever
 * the loaded libraries change); every other character must match exactly. */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char filebuf[65536];

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(filebuf, 1, sizeof filebuf - 1, f);
    fclose(f);
    filebuf[n] = 0;
    char *r = filebuf, *w = filebuf;          /* strip ;; comments */
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    int err = 0;
    cell b = r0_s1_parse(filebuf, &err);
    if (err) return -2;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -3;
}

/* the page's per-Run environment (primer-host.js: fresh instance, glon_init,
 * glon_load bootstrap + case [+ tuple-space]) */
static int fresh_env(int tasks) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    if (load_file("demo/shop/bootstrap.glon") != 0) return -1;
    if (load_file("demo/shop/case.glon") != 0) return -1;
    if (tasks && load_file("demo/shop/demos/tuple-space.glon") != 0) return -1;
    return 0;
}

static char manifest[65536];

/* got == expect, where one "<site>" in expect matches a positive integer */
static int outcome_matches(const char *got, const char *expect) {
    const char *ph = strstr(expect, "<site>");
    if (!ph) return strcmp(got, expect) == 0;
    size_t pre = (size_t)(ph - expect);
    const char *post = ph + 6;
    if (strncmp(got, expect, pre) != 0) return 0;
    const char *d = got + pre;
    if (*d < '1' || *d > '9') return 0;           /* positive, no leading 0 */
    while (*d >= '0' && *d <= '9') d++;
    return strcmp(d, post) == 0;
}

int run_r0_s1_primer_tests(void) {
    printf("R0-S1 primer doc-tests (demo/shop/primer.txt)\n");

    FILE *f = fopen("demo/shop/primer.txt", "rb");
    CHECK(f != NULL, "manifest demo/shop/primer.txt is readable");
    if (!f) return failures;
    size_t n = fread(manifest, 1, sizeof manifest - 1, f);
    fclose(f);
    manifest[n] = 0;

    /* line-oriented parse of the @example ... @end records */
    char id[64] = "", src[4096], expect[512], msg[5200], got[4096];
    char seen[64][64];
    int nseen = 0, examples = 0, tasks = 0, mode = 0;  /* 0 none, 1 source, 2 expect */
    int in_ex = 0;
    size_t slen = 0;
    expect[0] = 0;
    for (char *line = strtok(manifest, "\n"); line; line = strtok(NULL, "\n")) {
        size_t L = strlen(line);
        if (L && line[L - 1] == '\r') line[--L] = 0;
        if (strncmp(line, "@example ", 9) == 0) {
            snprintf(id, sizeof id, "%s", line + 9);
            in_ex = 1; tasks = 0; mode = 0; slen = 0; src[0] = 0; expect[0] = 0;
            continue;
        }
        if (!in_ex) continue;
        if (strcmp(line, "@env tasks") == 0) { tasks = 1; continue; }
        if (strcmp(line, "@source") == 0) { mode = 1; continue; }
        if (strcmp(line, "@expect") == 0) { mode = 2; continue; }
        if (strcmp(line, "@end") == 0) {
            examples++;
            int dup = 0;
            for (int k = 0; k < nseen; k++) if (strcmp(seen[k], id) == 0) dup = 1;
            if (nseen < 64) snprintf(seen[nseen++], sizeof seen[0], "%s", id);
            if (dup || slen == 0 || !expect[0]) {
                snprintf(msg, sizeof msg, "%s: well-formed (unique id, source, expect)", id);
                CHECK(0, msg);
            } else if (fresh_env(tasks) != 0) {
                snprintf(msg, sizeof msg, "%s: environment loads", id);
                CHECK(0, msg);
            } else {
                r0_s1_show_run(src, (unsigned)slen, got, sizeof got);
                int ok = outcome_matches(got, expect);
                if (ok) snprintf(msg, sizeof msg, "%s => %s", id, got);
                else snprintf(msg, sizeof msg, "%s: expected \"%s\", got \"%s\"", id, expect, got);
                CHECK(ok, msg);
            }
            in_ex = 0; mode = 0;
            continue;
        }
        if (mode == 1 && slen + L + 2 < sizeof src) {
            memcpy(src + slen, line, L);
            slen += L;
            src[slen++] = '\n';
            src[slen] = 0;
        } else if (mode == 2 && !expect[0]) {
            snprintf(expect, sizeof expect, "%s", line);
        }
    }
    snprintf(msg, sizeof msg, "manifest has %d examples (at least 20)", examples);
    CHECK(examples >= 20, msg);

    /* the "Coming from Rebol or Red" table's claims, checked the same way */
    static const struct { const char *src, *expect, *what; } claims[] = {
        { "+ 1 * 2 3", "7", "prefix, no precedence" },
        { "( + 1 2 )", "** halted (no SIN!: a machine-level fail-stop)", "no parentheses" },
        { "either 0 [ 1 ] [ 2 ]", "2", "0 is false" },
        { "either none [ 1 ] [ 2 ]", "2", "none is false" },
        { "true", "** halted (no SIN!: a machine-level fail-stop)", "no true word" },
        { "foreach x [ 1 2 ] [ x ]", "** halted (no SIN!: a machine-level fail-stop)", "no foreach" },
        { "switch 1 [ 1 [ 2 ] ]", "** halted (no SIN!: a machine-level fail-stop)", "no switch" },
        { "type? 1", "** halted (no SIN!: a machine-level fail-stop)", "no type?" },
        { "unset-word", "** halted (no SIN!: a machine-level fail-stop)", "unset word halts (not a SIN!)" },
        { "y: 5  f: func [] [ y: 1 ]  f  y", "1", "set-word updates the existing binding" },
        { "f: func [] [ y: 1 ]  f  y", "** halted (no SIN!: a machine-level fail-stop)", "else it makes a local" },
        { "= [ 1 ] [ 1 ]", "0", "= is identity for blocks" },
        { "str-eq \"tea\" \"tea\"", "1", "str-eq compares strings" },
    };
    for (unsigned k = 0; k < sizeof claims / sizeof claims[0]; k++) {
        if (fresh_env(0) != 0) { CHECK(0, "claims: environment loads"); continue; }
        r0_s1_show_run(claims[k].src, (unsigned)strlen(claims[k].src), got, sizeof got);
        int ok = strcmp(got, claims[k].expect) == 0;
        if (ok) snprintf(msg, sizeof msg, "table: %s (%s => %s)", claims[k].what, claims[k].src, got);
        else snprintf(msg, sizeof msg, "table: %s: `%s` expected \"%s\", got \"%s\"",
                      claims[k].what, claims[k].src, claims[k].expect, got);
        CHECK(ok, msg);
    }

    /* the <site> matcher is exactly one positive integer, nothing looser */
    const char *pat = "** uncaught #[SIN! escape return <site>]";
    CHECK(outcome_matches("** uncaught #[SIN! escape return 31]", pat) &&
          outcome_matches("** uncaught #[SIN! escape return 1207]", pat) &&
          !outcome_matches("** uncaught #[SIN! escape return ]", pat) &&
          !outcome_matches("** uncaught #[SIN! escape return 0]", pat) &&
          !outcome_matches("** uncaught #[SIN! escape return -3]", pat) &&
          !outcome_matches("** uncaught #[SIN! escape return x]", pat) &&
          !outcome_matches("** uncaught #[SIN! escape store 31]", pat) &&
          !outcome_matches("** uncaught #[SIN! escape return 31 ]", pat) &&
          !outcome_matches("** uncaught #[SIN! type return 31]", pat),
          "<site> matches only one positive integer; type, id and framing stay exact");

    if (failures == 0) printf("all primer doc-tests passed\n");
    return failures;
}
