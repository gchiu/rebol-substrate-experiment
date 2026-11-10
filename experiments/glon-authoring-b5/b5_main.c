/* B5: LLM authoring harness for the B4 representations. */
#include "bench_util.h"
#include "b4_common.h"
#include "b4_tagged.h"
#include "b4_direct.h"
#include "b3_compiler.h"
#include "b3_machine.h"
#include "r0_s1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODE_BASE 256
#define CODE_CAP 4096

typedef struct {
    const char *name;
    long (*ref)(void);
} Task;

static long ref_tri(void) { long s = 0; for (int i = 1; i <= 10; i++) s += i; return s; }
static long ref_pow2(void) { long v = 1; for (int i = 0; i < 10; i++) v *= 2; return v; }
static long ref_min3(void) { long a = 9, b = 4, c = 7; long m = a; if (b < m) m = b; if (c < m) m = c; return m; }
static long ref_clamp(void) { long x = 15, lo = 3, hi = 10; return x < lo ? lo : x > hi ? hi : x; }
static long ref_sign_neg(void) { long x = -7; return x < 0 ? -1 : x == 0 ? 0 : 1; }
static long ref_range_sum(void) { long s = 0; for (int i = 3; i <= 9; i++) s += i; return s; }
static long ref_choose(void) {
    long n = 6, k = 2, c = 1;
    if (k > n - k) k = n - k;
    for (long i = 1; i <= k; i++) c = c * (n - k + i) / i;
    return c;
}
static long ref_is_even(void) { return (10 % 2 == 0) ? 1 : 0; }
static long ref_ack_rec(long m, long n) {
    if (m == 0) return n + 1;
    if (n == 0) return ref_ack_rec(m - 1, 1);
    return ref_ack_rec(m - 1, ref_ack_rec(m, n - 1));
}
static long ref_ack(void) { return ref_ack_rec(2, 3); }
static long ref_quad(void) { return 5 * 4; }
static long ref_median3(void) {
    long a = 9, b = 2, c = 7, t;
    if (a > b) { t = a; a = b; b = t; }
    if (b > c) { t = b; b = c; c = t; }
    if (a > b) { t = a; a = b; b = t; }
    return b;
}
static long ref_max_neg(void) { long a = -9, b = -4, c = -7; long m = a; if (b > m) m = b; if (c > m) m = c; return m; }

static const Task tasks[] = {
    {"tri", ref_tri},
    {"pow2", ref_pow2},
    {"min3", ref_min3},
    {"clamp", ref_clamp},
    {"sign_neg", ref_sign_neg},
    {"range_sum", ref_range_sum},
    {"choose", ref_choose},
    {"is_even", ref_is_even},
    {"ack", ref_ack},
    {"quad", ref_quad},
    {"median3", ref_median3},
    {"max_neg", ref_max_neg}
};
#define NTASK ((int)(sizeof tasks / sizeof tasks[0]))
#define NREP 3
static const char *repname[NREP] = {"A:text", "B:tagged", "C:direct"};

typedef struct {
    unsigned char *data;
    size_t size;
} File;

static int read_file(const char *path, File *f) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < 0) { fclose(fp); return 0; }
    f->data = malloc((size_t)n + 1);
    if (!f->data) { fclose(fp); return 0; }
    f->size = fread(f->data, 1, (size_t)n, fp);
    fclose(fp);
    f->data[f->size] = 0;
    return 1;
}

static void free_file(File *f) { free(f->data); f->data = NULL; f->size = 0; }


/* Text token count: words, integers, '[' and ']' are tokens; ';' comments
 * are removed. This mirrors the B3 lexer's token boundaries for the subset. */
static long text_tokens(const char *s) {
    long n = 0;
    while (*s) {
        if (*s == ';') { while (*s && *s != '\n') s++; continue; }
        if (s[0] == '[' || s[0] == ']') { n++; s++; continue; }
        if (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') { s++; continue; }
        n++;
        while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' &&
               *s != '[' && *s != ']' && *s != ';') s++;
    }
    return n;
}

static int compile_rep(int rep, const File *f, b4_code *code, char *err, size_t errlen) {
    if (rep == 0) {
        b3_code c;
        if (!b3_compile((const char *)f->data, B4_FLAGS, B3_CONV_RETSTACK, &c, err, errlen))
            return 0;
        code->entry = c.entry;
        code->code_cells = c.code_cells;
        return 1;
    }
    if (rep == 1) return b4_tagged_compile(f->data, f->size, code, err, errlen);
    return b4_direct_assemble(f->data, f->size, code, err, errlen);
}

static int run_code(const b4_code *code, long *value, int check_balance) {
    s1_reset();
#ifdef B5_COUNT
    b3_reset_counts();
#endif
    s1_run(code->entry);
    *value = int_val(s1_top());
    return !check_balance || (M[REG_SP] == 16384 - 1 && M[REG_RP] == 24576);
}

static int snapshot(const b4_code *code, cell *buf, long *cells) {
    if (code->code_cells < 0 || code->code_cells > CODE_CAP) return 0;
    memcpy(buf, M + CODE_BASE, (size_t)code->code_cells * sizeof(cell));
    *cells = code->code_cells;
    return 1;
}

static int failures;

static void check(const char *what, int ok, const char *detail) {
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s%s%s\n", what, detail ? ": " : "", detail ? detail : ""); failures++; }
}

static void path_for(char *buf, size_t cap, const char *name, int rep) {
    const char *ext = rep == 0 ? ".glon" : rep == 1 ? ".b4b" : ".b4c";
    snprintf(buf, cap, "tasks/%s%s", name, ext);
}

static void tests(void) {
    printf("B5 authoring task tests\n");
    for (int ti = 0; ti < NTASK; ti++) {
        cell first[NREP][CODE_CAP];
        long first_cells[NREP] = {0};
        long expected = tasks[ti].ref();
        int ok = 1;
        char detail[1024] = "";
        for (int r = 0; r < NREP; r++) {
            char path[512], err[256];
            File f;
            b4_code code;
            path_for(path, sizeof path, tasks[ti].name, r);
            if (!read_file(path, &f)) {
                ok = 0; snprintf(detail, sizeof detail, "%s: cannot read %s", repname[r], path); break;
            }
            if (!compile_rep(r, &f, &code, err, sizeof err)) {
                ok = 0; snprintf(detail, sizeof detail, "%s: %s", repname[r], err); free_file(&f); break;
            }
            long got = 0;
            if (!run_code(&code, &got, 1) || got != expected) {
                ok = 0; snprintf(detail, sizeof detail, "%s: result %ld, wanted %ld, or stack imbalance",
                                 repname[r], got, expected);
                free_file(&f); break;
            }
            if (!snapshot(&code, first[r], &first_cells[r])) {
                ok = 0; snprintf(detail, sizeof detail, "%s: code too large", repname[r]); free_file(&f); break;
            }
            /* Deterministic recompile. */
            b4_code again;
            cell second[CODE_CAP]; long second_cells = 0;
            if (!compile_rep(r, &f, &again, err, sizeof err) ||
                !snapshot(&again, second, &second_cells) ||
                second_cells != first_cells[r] ||
                memcmp(second, first[r], (size_t)first_cells[r] * sizeof(cell)) != 0) {
                ok = 0; snprintf(detail, sizeof detail, "%s: nondeterministic compilation", repname[r]);
                free_file(&f); break;
            }
            free_file(&f);
        }
        if (ok && !(first_cells[0] == first_cells[1] && first_cells[1] == first_cells[2] &&
                    memcmp(first[0], first[1], (size_t)first_cells[0] * sizeof(cell)) == 0 &&
                    memcmp(first[0], first[2], (size_t)first_cells[0] * sizeof(cell)) == 0)) {
            ok = 0; snprintf(detail, sizeof detail, "A/B/C generated different code");
        }
        check(tasks[ti].name, ok, detail[0] ? detail : NULL);
    }

    /* Negative compiler tests. */
    {
        b3_code c; char err[256];
        check("A rejects unsupported text",
              !b3_compile("f: func [x] [ y ] f 1", B4_FLAGS, B3_CONV_RETSTACK, &c, err, sizeof err), NULL);
    }

    {
        unsigned char b[4096];
        size_t n;
        char err[256];
        b4_code c;
        #define PUT(i, role, arg) do { unsigned w = ((unsigned)(role) << 24) | ((unsigned)(arg) & 0xFFFFFFu); \
            b[4*(i)] = (unsigned char)(w); b[4*(i)+1] = (unsigned char)(w >> 8); \
            b[4*(i)+2] = (unsigned char)(w >> 16); b[4*(i)+3] = (unsigned char)(w >> 24); } while (0)
        PUT(0, 0, 0); n = 4;
        check("B rejects bad magic", !b4_tagged_compile(b, n, &c, err, sizeof err), NULL);
        PUT(0, 0, B4B_MAGIC); PUT(1, 99, 0); n = 8;
        check("B rejects unknown role", !b4_tagged_compile(b, n, &c, err, sizeof err), NULL);
        n = 6;
        check("B rejects truncated words", !b4_tagged_compile(b, n, &c, err, sizeof err), NULL);
        /* duplicate function id: FUNC 1/0, body int 0, FUNC 1/0 */
        PUT(0, 0, B4B_MAGIC); PUT(1, BT_FUNC, (1<<8)|0); PUT(2, BT_SEQ_BEGIN, 0);
        PUT(3, BT_INT, 0); PUT(4, BT_SEQ_END, 0); PUT(5, BT_FUNC, (1<<8)|0);
        n = 6*4;
        check("B rejects duplicate function", !b4_tagged_compile(b, n, &c, err, sizeof err), NULL);
        /* undefined call: f calls id 2, top calls f */
        PUT(0, 0, B4B_MAGIC); PUT(1, BT_FUNC, (1<<8)|0); PUT(2, BT_SEQ_BEGIN, 0);
        PUT(3, BT_CALL, (2<<8)|0); PUT(4, BT_SEQ_END, 0); PUT(5, BT_TOP_BEGIN, 0);
        PUT(6, BT_CALL, (1<<8)|0); PUT(7, BT_TOP_END, 0); n = 8*4;
        check("B rejects undefined call", !b4_tagged_compile(b, n, &c, err, sizeof err), NULL);
        /* out-of-range parameter: function arity 0 uses param 0 */
        PUT(0, 0, B4B_MAGIC); PUT(1, BT_FUNC, (1<<8)|0); PUT(2, BT_SEQ_BEGIN, 0);
        PUT(3, BT_PARAM, 0); PUT(4, BT_SEQ_END, 0); PUT(5, BT_TOP_BEGIN, 0);
        PUT(6, BT_INT, 0); PUT(7, BT_TOP_END, 0); n = 8*4;
        check("B rejects out-of-range parameter", !b4_tagged_compile(b, n, &c, err, sizeof err), NULL);
        /* call arity mismatch: definition arity 0, call token says 1 arg */
        PUT(0, 0, B4B_MAGIC); PUT(1, BT_FUNC, (1<<8)|0); PUT(2, BT_SEQ_BEGIN, 0);
        PUT(3, BT_INT, 0); PUT(4, BT_SEQ_END, 0); PUT(5, BT_TOP_BEGIN, 0);
        PUT(6, BT_CALL, (1<<8)|1); PUT(7, BT_INT, 0); PUT(8, BT_TOP_END, 0); n = 9*4;
        check("B rejects call-arity mismatch", !b4_tagged_compile(b, n, &c, err, sizeof err), NULL);
        #undef PUT
    }

    {
        unsigned char b[4096];
        b4_code c; char err[256];
        #define CPUT(i, role, arg) do { unsigned w = ((unsigned)(role) << 24) | ((unsigned)(arg) & 0xFFFFFFu); \
            b[4*(i)] = (unsigned char)(w); b[4*(i)+1] = (unsigned char)(w >> 8); \
            b[4*(i)+2] = (unsigned char)(w >> 16); b[4*(i)+3] = (unsigned char)(w >> 24); } while (0)
        CPUT(0, 0, 0);
        check("C rejects bad magic", !b4_direct_assemble(b, 4, &c, err, sizeof err), NULL);
        CPUT(0, 0, B4C_MAGIC); CPUT(1, 99, 0);
        check("C rejects unknown operation", !b4_direct_assemble(b, 8, &c, err, sizeof err), NULL);
        CPUT(0, 0, B4C_MAGIC); CPUT(1, BC_ENTRY, 9); CPUT(2, BC_HALT, 0);
        check("C rejects undefined entry", !b4_direct_assemble(b, 12, &c, err, sizeof err), NULL);
        CPUT(0, 0, B4C_MAGIC); CPUT(1, BC_LABEL, 1); CPUT(2, BC_HALT, 0);
        check("C rejects missing entry", !b4_direct_assemble(b, 12, &c, err, sizeof err), NULL);
        CPUT(0, 0, B4C_MAGIC); CPUT(1, BC_ENTRY, 1); CPUT(2, BC_LABEL, 1);
        CPUT(3, BC_LABEL, 1); CPUT(4, BC_HALT, 0);
        check("C rejects duplicate label", !b4_direct_assemble(b, 20, &c, err, sizeof err), NULL);
        CPUT(0, 0, B4C_MAGIC); CPUT(1, BC_ENTRY, 1); CPUT(2, BC_LABEL, 1);
        CPUT(3, BC_CALL, 2); CPUT(4, BC_HALT, 0);
        check("C rejects undefined label", !b4_direct_assemble(b, 20, &c, err, sizeof err), NULL);
        #undef CPUT
    }

    printf("\nB5 tests: %d failure(s)\n", failures);
}

#ifdef B5_COUNT
static void measure(void) {
    printf("B5 counts (source -> code -> execution)\n");
    for (int ti = 0; ti < NTASK; ti++) {
        for (int r = 0; r < NREP; r++) {
            char path[512], err[256];
            File f; b4_code code;
            path_for(path, sizeof path, tasks[ti].name, r);
            if (!read_file(path, &f) || !compile_rep(r, &f, &code, err, sizeof err)) {
                printf("%-8s %-8s ERROR\n", tasks[ti].name, repname[r]); free_file(&f); continue;
            }
            long got = 0;
            int bal = run_code(&code, &got, 1);
            long tokens = r == 0 ? text_tokens((const char *)f.data)
                                 : (long)(f.size / 4 - 1);
            printf("%-8s %-8s bytes %4lu tokens %3ld cells %3ld insns %8ld result %-7ld ds/rs %ld/%ld %s\n",
                   tasks[ti].name, repname[r], (unsigned long)f.size, tokens, code.code_cells,
                   b3_insns, got, 16384 - b3_min_sp, 24576 - b3_min_rp, bal ? "ok" : "STACK");
            free_file(&f);
        }
    }
}
#else
typedef struct { cell entry; } RunCtx;
static void run_cb(void *v) { RunCtx *c = v; s1_reset(); s1_run(c->entry); }

typedef struct { int rep; const File *f; } CompileCtx;
static void compile_cb(void *v) {
    CompileCtx *c = v;
    b4_code code; char err[256];
    (void)compile_rep(c->rep, c->f, &code, err, sizeof err);
}

static void bench(void) {
    printf("B5 source-processing and execution times (median us)\n");
    for (int ti = 0; ti < NTASK; ti++) {
        long expected = tasks[ti].ref();
        for (int r = 0; r < NREP; r++) {
            char path[512], err[256];
            File f; b4_code code;
            path_for(path, sizeof path, tasks[ti].name, r);
            if (!read_file(path, &f) || !compile_rep(r, &f, &code, err, sizeof err)) {
                printf("%-8s %-8s ERROR\n", tasks[ti].name, repname[r]); free_file(&f); continue;
            }
            long got = 0;
            if (!run_code(&code, &got, 1) || got != expected) {
                printf("%-8s %-8s VERIFY FAILED\n", tasks[ti].name, repname[r]); free_file(&f); continue;
            }
            CompileCtx cc = { r, &f };
            bench_stats cs = bench_measure(compile_cb, &cc, 0.02, 7);
            RunCtx rc = { code.entry };
            bench_stats rs = bench_measure(run_cb, &rc, 0.05, 9);
            long tokens = r == 0 ? text_tokens((const char *)f.data)
                                 : (long)(f.size / 4 - 1);
            printf("%-8s %-8s bytes %4lu tokens %3ld cells %3ld  source %8.2f us  exec %8.2f us  iters %ld\n",
                   tasks[ti].name, repname[r], (unsigned long)f.size, tokens, code.code_cells,
                   cs.median * 1e6, rs.median * 1e6, rs.iters);
            free_file(&f);
        }
    }
}
#endif

static int verify_one(const char *task, const char *rep, const char *path) {
    int ti = -1, r = -1;
    for (int i = 0; i < NTASK; i++) if (!strcmp(tasks[i].name, task)) ti = i;
    if (!strcmp(rep, "A")) r = 0; else if (!strcmp(rep, "B")) r = 1;
    else if (!strcmp(rep, "C")) r = 2;
    if (ti < 0 || r < 0) { printf("unknown task or representation\n"); return 2; }
    File f; b4_code code; char err[256];
    if (!read_file(path, &f)) { printf("cannot read %s\n", path); return 2; }
    if (!compile_rep(r, &f, &code, err, sizeof err)) {
        printf("COMPILE FAIL: %s\n", err); free_file(&f); return 1;
    }
    long got = 0;
    int bal = run_code(&code, &got, 1);
    free_file(&f);
    long expected = tasks[ti].ref();
    printf("result %ld, expected %ld, stack %s\n", got, expected, bal ? "ok" : "IMBALANCE");
    return (got == expected && bal) ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--test")) { tests(); return failures ? 1 : 0; }
    if (argc > 1 && !strcmp(argv[1], "--measure")) {
#ifdef B5_COUNT
        measure();
#else
        printf("measure mode needs the counter build\n");
#endif
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--bench")) {
#ifndef B5_COUNT
        bench();
#else
        printf("bench mode needs the non-counter build\n");
#endif
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--verify")) return verify_one(argv[2], argv[3], argv[4]);
    printf("usage: b4 --test | --measure | --bench | --verify TASK A|B|C FILE\n");
    return 2;
}
