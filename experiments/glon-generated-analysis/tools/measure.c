/* Measure arbitrary text/tagged artifacts on the B3/B4 common target. */
#include "bench_util.h"
#include "b3_compiler.h"
#include "b3_machine.h"
#include "b4_tagged.h"
#include "r0_s1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODE_BASE 256

typedef struct { cell entry; long cells; int tagged; const unsigned char *data; size_t n; } Ctx;

static int compile_ctx(void *v, char *err, size_t errlen) {
    Ctx *c = v;
    if (c->tagged) {
        b4_code b;
        if (!b4_tagged_compile(c->data, c->n, &b, err, errlen)) return 0;
        c->entry = b.entry; c->cells = b.code_cells; return 1;
    }
    b3_code b;
    if (!b3_compile((const char *)c->data, B4_FLAGS, B3_CONV_RETSTACK, &b, err, errlen)) return 0;
    c->entry = b.entry; c->cells = b.code_cells; return 1;
}

static unsigned long long code_hash(long cells) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned char *p = (unsigned char *)(M + CODE_BASE);
    size_t n = (size_t)cells * sizeof(cell);
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

#ifndef B3_COUNT
typedef struct { cell entry; } RunCtx;
static void run_cb(void *v) {
    RunCtx *r = v;
    s1_reset();
    s1_run(r->entry);
}
static void compile_cb(void *v) {
    Ctx *c = v; char err[256];
    (void)compile_ctx(c, err, sizeof err);
}
#endif

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[1], "text") && strcmp(argv[1], "tagged"))) {
        fprintf(stderr, "usage: %s text|tagged FILE\n", argv[0]); return 2;
    }
    int tagged = !strcmp(argv[1], "tagged");
    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)n + 1);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(data); return 2; }
    fclose(f); data[n] = 0;
    Ctx c = {0,0,tagged,data,(size_t)n};
    char err[512];
    if (!compile_ctx(&c, err, sizeof err)) {
        printf("{\"error\":\"%s\"}\n", err);
        free(data); return 1;
    }
#ifdef B3_COUNT
    s1_reset();
    b3_reset_counts();
    s1_run(c.entry);
    printf("{\"mode\":\"%s\",\"cells\":%ld,\"instructions\":%ld,"
           "\"result\":%ld,\"stack_ok\":%s,\"code_hash\":\"%016llx\","
           "\"ds\":%ld,\"rs\":%ld}\n",
           tagged ? "tagged" : "text", c.cells, b3_insns, int_val(s1_top()),
           (M[REG_SP] == 16384 - 1 && M[REG_RP] == 24576) ? "true" : "false",
           code_hash(c.cells), 16384 - b3_min_sp, 24576 - b3_min_rp);
#else
    RunCtx r = { c.entry };
    bench_stats bs = bench_measure(run_cb, &r, 0.05, 9);
    bench_stats cs = bench_measure(compile_cb, &c, 0.02, 7);
    printf("{\"mode\":\"%s\",\"cells\":%ld,\"exec_median_us\":%.3f,"
           "\"exec_min_us\":%.3f,\"exec_max_us\":%.3f,\"exec_spread_pct\":%.2f,"
           "\"compile_median_us\":%.3f,\"code_hash\":\"%016llx\"}\n",
           tagged ? "tagged" : "text", c.cells, bs.median*1e6, bs.min*1e6,
           bs.max*1e6, bs.spread, cs.median*1e6, code_hash(c.cells));
#endif
    free(data);
    return 0;
}
