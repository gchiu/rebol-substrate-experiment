/* r0_s1_p10a_bench.c - FIB-OPT-P10A: compiled-S1 baseline benchmark driver.
 *
 * Measures the cost of the interpreted S1 layer by generating a mechanical
 * computed-goto ("direct threading") C transcription of the frozen S1 stream,
 * compiling it (-O2, shared object), and executing it against the same M[] as
 * the interpreted s1_run(). This is an architectural measurement only -- it is
 * not a production compiler and it changes no Glon/S1 semantics.
 *
 * The generated code keeps IP/SP/RP/HP memory-mapped (no register promotion --
 * that is P10B), uses the frozen host() dispatch verbatim, and preserves branch
 * targets, RAW fragments, and the activation-frame ABI.
 */
#define _POSIX_C_SOURCE 200809L
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/stat.h>

#define CODE_BASE 256          /* s1.c CODE_BASE (code region starts here) */

static const char *FIB_DEF =
    "fib: func [n] [ either <= n 1 [ n ] [ + fib - n 1 fib - n 2 ] ]";
static const char *TREE_DEF =
    "t: func [n] [ either < n 2 [ n ] [ + t - n 1 t - n 2 ] ]";
static const char *RAW_DEF =
    "[ add2: raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD LIT 16 MUL ARITY 1 EXIT ] "
    " add2 3 4 ]";

static char prog[4096];

/* ------------------------------------------------------------------ */
/* Emit a literal computed-goto transcription of M[CODE_BASE .. code_end). */

static void emit_host(FILE *f) {
    fputs(
"static void host(cell id) {\n"
"    switch (id) {\n"
"    case 0:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = a + b; break; }\n"
"    case 1:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = a - b; break; }\n"
"    case 2:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = a * b; break; }\n"
"    case 3:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = a / b; break; }\n"
"    case 4:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = a % b; break; }\n"
"    case 5:  { cell v = M[M[1]++]; M[--M[1]] = -v; break; }\n"
"    case 6:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = (a == b) ? 1 : 0; break; }\n"
"    case 7:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = (a != b) ? 1 : 0; break; }\n"
"    case 8:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = (a <  b) ? 1 : 0; break; }\n"
"    case 9:  { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = (a >  b) ? 1 : 0; break; }\n"
"    case 10: { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = (a <= b) ? 1 : 0; break; }\n"
"    case 11: { cell b = M[M[1]++]; cell a = M[M[1]++]; M[--M[1]] = (a >= b) ? 1 : 0; break; }\n"
"    case 12: { cell n = M[M[1]++]; cell a = M[3]; M[3] += n; M[--M[1]] = a; break; }\n"
"    case 13: { putchar((int)M[M[1]++]); fflush(stdout); break; }\n"
"    case 14: { printf(\"%ld\\n\", (long)M[M[1]++]); fflush(stdout); break; }\n"
"    case 15: { fprintf(stderr, \"[dump] IP=%ld SP=%ld RP=%ld HP=%ld top=%ld\\n\",\n"
"                    (long)M[0], (long)M[1], (long)M[2], (long)M[3], (long)M[M[1]]); break; }\n"
"    default: { fprintf(stderr, \"s1: bad host id %ld\\n\", (long)id); break; }\n"
"    }\n"
"}\n", f);
}

static void emit_compiled_c(FILE *f, cell code_end) {
    cell n = code_end - CODE_BASE;
    /* instruction starts (bool per address in [CODE_BASE, code_end)) */
    unsigned char *instr = (unsigned char *)calloc((size_t)(n > 0 ? n : 1), 1);

    /* first pass: mark instruction starts */
    for (cell a = CODE_BASE; a < code_end; ) {
        instr[a - CODE_BASE] = 1;
        cell op = M[a];
        if (op == OP_LIT || op == OP_ZBRANCH || op == OP_HOST) a += 2;
        else a += 1;
    }

    fputs("#include <stdint.h>\n#include <stdio.h>\ntypedef intptr_t cell;\n"
          "static cell *M;\n\n", f);
    emit_host(f);

    fputs("void compiled_run(cell *vm, cell start) {\n"
          "    M = vm;\n"
          "    static void *const T[] = {\n", f);

    /* dispatch table: one entry per address in [CODE_BASE, code_end) */
    for (cell a = CODE_BASE; a < code_end; a++) {
        if (instr[a - CODE_BASE])
            fprintf(f, "        &&L_%ld,\n", (long)a);
        else
            fputs("        &&L_bad,\n", f);
    }
    fputs("    };\n"
          "    M[0] = start;\n"
          "    goto *T[M[0] - 256];\n\n", f);

    /* second pass: emit labels + literal operations */
    for (cell a = CODE_BASE; a < code_end; ) {
        cell op = M[a];
        fprintf(f, "L_%ld:\n", (long)a);
        switch (op) {
        case OP_LIT:
            fprintf(f, "    M[--M[1]] = %ld;\n    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n",
                    (long)M[a + 1], (long)(a + 2));
            a += 2; break;
        case OP_DUP:
            fputs("    { cell v = M[M[1]]; M[--M[1]] = v; }\n", f);
            fprintf(f, "    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n", (long)(a + 1));
            a += 1; break;
        case OP_DROP:
            fputs("    M[1]++;\n", f);
            fprintf(f, "    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n", (long)(a + 1));
            a += 1; break;
        case OP_FETCH:
            fputs("    { cell _a = M[M[1]++]; cell v = M[_a]; M[--M[1]] = v; }\n", f);
            fprintf(f, "    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n", (long)(a + 1));
            a += 1; break;
        case OP_STORE:
            /* STORE writes M[a]=v where a is a runtime value, so it may write
             * REG_IP (M[0]) and thus change control flow (BRANCH/CALL/EXIT are
             * all stores to IP). Set the fall-through address first, then let
             * the store possibly override M[0]. */
            fprintf(f, "    M[0] = %ld;\n", (long)(a + 1));
            fputs("    { cell _a = M[M[1]++]; cell v = M[M[1]++]; M[_a] = v; }\n", f);
            fputs("    goto *T[M[0] - 256];\n\n", f);
            a += 1; break;
        case OP_ZBRANCH:
            fprintf(f, "    { cell f = M[M[1]++]; M[0] = (f == 0) ? %ld : %ld; }\n",
                    (long)M[a + 1], (long)(a + 2));
            fputs("    goto *T[M[0] - 256];\n\n", f);
            a += 2; break;
        case OP_HOST:
            fprintf(f, "    host(%ld);\n    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n",
                    (long)M[a + 1], (long)(a + 2));
            a += 2; break;
        case OP_HALT:
            fputs("    return;\n\n", f);
            a += 1; break;
        default:
            fputs("    return;\n\n", f);
            a += 1; break;
        }
    }

    fputs("L_bad:\n    fprintf(stderr, \"compiled: bad dispatch at %ld\\n\", (long)M[0]);\n"
          "    return;\n}\n", f);
    free(instr);
}

/* ------------------------------------------------------------------ */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(void) {
    cell main_entry = r0_s1_init();
    (void)main_entry;

    /* Parse the workloads. */
    int err;
    snprintf(prog, sizeof prog, "[ %s fib 25 ]", FIB_DEF);
    cell fib_block = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "fib parse error\n"); return 1; }

    snprintf(prog, sizeof prog, "[ %s t 25 ]", TREE_DEF);
    cell tree_block = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "tree parse error\n"); return 1; }

    snprintf(prog, sizeof prog, "%s", RAW_DEF);
    cell raw_block = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "raw parse error\n"); return 1; }

    /* The whole S1 stream (evaluator + any RAW fragments) is fixed now. */
    cell code_end = asm_here();

    /* Generate + compile the compiled-S1 shared object (skip if up-to-date). */
    struct stat src_st, so_st;
    int need_build = 1;
    if (stat("/tmp/opencode_compiled_s1.c", &src_st) == 0 &&
        stat("/tmp/opencode_compiled_s1.so", &so_st) == 0 &&
        so_st.st_mtime >= src_st.st_mtime)
        need_build = 0;

    double c0 = now_sec();
    if (need_build) {
        FILE *cf = fopen("/tmp/opencode_compiled_s1.c", "w");
        if (!cf) { fprintf(stderr, "cannot write /tmp/opencode_compiled_s1.c\n"); return 1; }
        emit_compiled_c(cf, code_end);
        fclose(cf);
        int rc = system("gcc -O2 -shared -fPIC /tmp/opencode_compiled_s1.c -o /tmp/opencode_compiled_s1.so");
        if (rc != 0) { fprintf(stderr, "gcc compile failed (rc=%d)\n", rc); return 1; }
    }
    double c1 = now_sec();
    if (need_build)
        printf("compile time: %.3f s (code cells %ld, instruction stream)\n",
               c1 - c0, (long)(code_end - CODE_BASE));

    void *h = dlopen("/tmp/opencode_compiled_s1.so", RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    void (*compiled_run)(cell *, cell) = (void (*)(cell *, cell))dlsym(h, "compiled_run");
    if (!compiled_run) { fprintf(stderr, "dlsym: %s\n", dlerror()); return 1; }

    /* Correctness: compiled must equal interpreted on several programs. */
    {
        cell blocks[3] = { fib_block, tree_block, raw_block };
        const char *names[3] = { "fib 25", "tree 25", "raw add2" };
        int ok = 1;
        for (int i = 0; i < 3; i++) {
            int Ni = r0_s1_run(blocks[i]);
            cell Ri = (Ni == 1) ? r0_s1_result(0, Ni) : -999;
            int Nc = r0_s1_run_compiled(blocks[i], compiled_run);
            cell Rc = (Nc == 1) ? r0_s1_result(0, Nc) : -999;
            int match = (Ni == Nc && Ri == Rc);
            printf("  %s: interpreted N=%d r=%ld  compiled N=%d r=%ld  %s\n",
                   names[i], Ni, (long)Ri, Nc, (long)Rc, match ? "MATCH" : "MISMATCH");
            if (!match) ok = 0;
        }
        if (!ok) { fprintf(stderr, "CORRECTNESS MISMATCH\n"); return 1; }
    }

    /* Timing: fib 25, alternating interpreted/compiled, warmup first. */
    r0_s1_run(fib_block);                       /* warmup interpreted */
    r0_s1_run_compiled(fib_block, compiled_run);/* warmup compiled */

    static double ti[8], tc[8];
    int got = 0;
    for (int r = 0; r < 8; r++) {
        double t0 = now_sec(); r0_s1_run(fib_block); double t1 = now_sec();
        ti[got] = t1 - t0;
        t0 = now_sec(); r0_s1_run_compiled(fib_block, compiled_run); t1 = now_sec();
        tc[got] = t1 - t0;
        got++;
    }
    for (int i = 0; i < got; i++)
        for (int j = i + 1; j < got; j++) {
            if (ti[j] < ti[i]) { double t = ti[i]; ti[i] = ti[j]; ti[j] = t; }
            if (tc[j] < tc[i]) { double t = tc[i]; tc[i] = tc[j]; tc[j] = t; }
        }
    double med_i = ti[got/2], med_c = tc[got/2];
    printf("fib 25 interpreted: median %.4f s (min %.4f, max %.4f)\n",
           med_i, ti[0], ti[got-1]);
    printf("fib 25 compiled:    median %.4f s (min %.4f, max %.4f)\n",
           med_c, tc[0], tc[got-1]);
    printf("speedup (interpreted/compiled): %.3fx\n", med_i / med_c);

    dlclose(h);
    return 0;
}
