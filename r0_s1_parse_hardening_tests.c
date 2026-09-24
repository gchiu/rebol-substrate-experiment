/* r0_s1_parse_hardening_tests.c - a long-lived session must survive parse and
 * load-resource failures.
 *
 * A persistent session (the WASM demos, a notebook kernel) parses one source
 * after another into ONE runtime. Parsing is the only thing that consumes the
 * permanent loader resources: the symbol table (syms[512]), the loader heap
 * [R0S1_HEAP_BASE, R0S1_HEAP_LIMIT) and the func-site table. A failed parse
 * must therefore:
 *   - never write out of bounds (no syms[] overflow, no write through a failed
 *     loader allocation);
 *   - say why it failed (r0_s1_parse_error_kind), not just "error";
 *   - be rolled back completely, so failures never accumulate;
 *   - leave the session running: earlier definitions still execute.
 * Capacities are deliberately NOT changed. These are load-time failures before
 * any Glon runs, so they are not SIN!s. */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static int nsyms(void) {
    int n = 0;
    while (r0_s1_sym_name(n)) n++;
    return n;
}

static void fresh(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}

/* run one cell in the session; returns the single integer result, or a
 * sentinel for "parse failed" / "did not end cleanly with one integer" */
#define PARSE_FAILED (-999999)
#define NOT_INT      (-888888)
static int last_kind;
static long cellv(const char *src) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    last_kind = r0_s1_parse_error_kind();
    if (err) return PARSE_FAILED;
    int n = r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly() || n != 1) return NOT_INT;
    cell v = r0_s1_result(0, n);
    return (v & 15) == T_INT ? (long)int_val(v) : NOT_INT;
}

static char big[400000];

int run_r0_s1_parse_hardening_tests(void) {
    printf("R0-S1 parse hardening (long-lived sessions)\n");

    /* ---- 1. symbol table: overflow is a clean, rolled-back failure --------- */
    fresh();
    CHECK(cellv("[ keep: 21  kf: func [n] [ * n 2 ]  0 ]") == 0, "S1: session state defined (keep, kf)");
    int before_fill = nsyms();
    int cells = 0;
    long r = 0;
    for (int k = 0; k < 100; k++) {
        /* 40 new words per cell, as block DATA (never bound), so this fills
         * the symbol table without also growing the global context */
        char *w = big;
        w += sprintf(w, "[ [ ");
        for (int j = 0; j < 40; j++) w += sprintf(w, "s%d-%d ", k, j);
        sprintf(w, "] 0 ]");
        cell hp = M[GC_LOADER_HP];
        int ns = nsyms();
        r = cellv(big);
        if (r == PARSE_FAILED) {
            char msg[200];
            snprintf(msg, sizeof msg, "S2: the symbol table fills after %d cells (%d of 512 symbols); "
                     "overflow is SYMBOL_TABLE_FULL, not an out-of-bounds write", cells, ns);
            CHECK(last_kind == R0S1_PARSE_SYMBOL_TABLE_FULL, msg);
            CHECK(M[GC_LOADER_HP] == hp && nsyms() == ns,
                  "S3: the failed cell is rolled back (loader heap and symbol count unchanged)");
            break;
        }
        cells++;
    }
    CHECK(r == PARSE_FAILED && before_fill < 512, "S4: the fill loop reached the symbol-table limit");
    CHECK(cellv("[ kf keep ]") == 42, "S5: earlier definitions still run after the overflow (kf keep -> 42)");
    {
        /* top the table up to exactly its capacity, one new word per cell */
        for (int k = 0; k < 100 && cellv("[ 0 ]") == 0; k++) {
            char src[80];
            snprintf(src, sizeof src, "[ [ topup-%d ] 0 ]", k);
            if (cellv(src) == PARSE_FAILED) break;
        }
        int full = nsyms();
        CHECK(full == 512, "S5b: the symbol table is now exactly full (512 of 512)");
        cell hp = M[GC_LOADER_HP];
        int all_clean = 1;
        for (int k = 0; k < 200; k++) {
            char src[80];
            snprintf(src, sizeof src, "[ [ brand-new-word-%d ] 1 ]", k);
            if (cellv(src) != PARSE_FAILED || last_kind != R0S1_PARSE_SYMBOL_TABLE_FULL) all_clean = 0;
        }
        CHECK(all_clean && nsyms() == full && M[GC_LOADER_HP] == hp,
              "S6: 200 further new-word cells all fail cleanly and consume nothing");
        CHECK(cellv("[ keep: + keep 1  keep ]") == 22, "S7: cells using known words still run (keep -> 22)");
    }

    /* ---- 2. syntax failures consume nothing ------------------------------- */
    fresh();
    CHECK(cellv("[ x: 21  0 ]") == 0, "P1: session state defined (x)");
    {
        cell hp = M[GC_LOADER_HP];
        int ns = nsyms();
        int all_syntax = 1;
        for (int k = 0; k < 5000; k++) {
            char src[120];
            /* unclosed block, a func literal (site) and a brand-new word each time */
            snprintf(src, sizeof src, "[ typo-%d: func [a] [ + a 1 ] [ unclosed", k);
            if (cellv(src) != PARSE_FAILED || last_kind != R0S1_PARSE_SYNTAX) all_syntax = 0;
        }
        CHECK(all_syntax, "P2: 5000 malformed cells each fail as SYNTAX");
        CHECK(M[GC_LOADER_HP] == hp, "P3: the loader heap is back at exactly the pre-cell mark");
        CHECK(nsyms() == ns, "P4: no symbols leak from failed cells (their new words are rolled back)");
        CHECK(cellv("[ f: func [n] [ * n 2 ]  f x ]") == 42,
              "P5: sites are not leaked either: a func literal still parses, and x survives (42)");
        CHECK(cellv("[ x ]") == 21 && last_kind == R0S1_PARSE_OK, "P6: a successful parse reports OK");
    }

    /* ---- 3. loader exhaustion: clean failure, rollback --------------------- */
    fresh();
    CHECK(cellv("[ x: 21  mk: func [n] [ func [] [ n: + n 1 ] ]  c: mk 0  0 ]") == 0,
          "L1: session state defined (x, a counter closure c)");
    CHECK(cellv("[ c ]") == 1, "L2: the counter runs before exhaustion (1)");
    {
        /* 80 sub-blocks of 400 integers: ~32000 loader cells, far more than the
         * loader heap can hold */
        char *w = big;
        w += sprintf(w, "[ ");
        for (int b = 0; b < 80; b++) {
            w += sprintf(w, "[ ");
            for (int j = 0; j < 400; j++) w += sprintf(w, "%d ", j);
            w += sprintf(w, "] ");
        }
        sprintf(w, "]");
        cell hp = M[GC_LOADER_HP];
        int ns = nsyms();
        CHECK(cellv(big) == PARSE_FAILED && last_kind == R0S1_PARSE_LOADER_EXHAUSTED,
              "L3: an oversized cell fails as LOADER_EXHAUSTED");
        CHECK(M[GC_LOADER_HP] == hp && nsyms() == ns, "L4: the loader heap is restored to the pre-cell mark");
        /* 4. resumability: the session is intact after the rollback */
        CHECK(cellv("[ * x 2 ]") == 42, "L5: resumable: earlier globals run (* x 2 -> 42)");
        CHECK(cellv("[ c ]") == 2, "L6: resumable: the closure keeps its own state (2)");
        CHECK(cellv("[ y: 5  + x y ]") == 26, "L7: resumable: new definitions still parse and run (26)");
        /* churn the managed heap (GC) after the failure */
        int churn = cellv("[ loop: func [n] [ either > n 0 [ mk n  loop - n 1 ] [ 0 ] ]  0 ]") == 0;
        for (int k = 0; k < 8; k++) if (cellv("[ loop 50 ]") != 0) churn = 0;
        CHECK(churn && cellv("[ c ]") == 3,
              "L8: resumable across garbage collection: 400 closures allocated, c still 3");
        int again = 1;
        cell hp2 = M[GC_LOADER_HP];
        for (int k = 0; k < 20; k++) if (cellv(big) != PARSE_FAILED) again = 0;
        CHECK(again && M[GC_LOADER_HP] == hp2 && cellv("[ c ]") == 4,
              "L9: repeated exhaustion stays clean and the session keeps working (c -> 4)");
    }

    /* ---- 4. site table: overflow is clean and rolled back ------------------ */
    fresh();
    {
        char *w = big;
        w += sprintf(w, "[ ");
        for (int k = 0; k < 700; k++) {            /* 5 sub-blocks of 140 literals */
            if (k % 140 == 0) w += sprintf(w, "%s[ ", k ? "] " : "");
            w += sprintf(w, "func [] %d ", k);   /* computed body: one site, no body block */
        }
        sprintf(w, "] ]");
        cell hp = M[GC_LOADER_HP];
        CHECK(cellv(big) == PARSE_FAILED && last_kind == R0S1_PARSE_SITE_TABLE_FULL &&
              M[GC_LOADER_HP] == hp,
              "T1: 700 func sites (> 640) fail as SITE_TABLE_FULL and are rolled back");
        CHECK(cellv("[ g: func [n] [ + n 1 ]  g 41 ]") == 42,
              "T2: the site table was rolled back: a new func still parses and runs (42)");
    }

    if (failures == 0) printf("all parse hardening tests passed\n");
    return failures;
}
