/* r0_s1_p10b_bench.c - FIB-OPT-P10B/P10C: register promotion + HOST intrinsics.
 *
 * Compares mechanically compiled versions of the frozen S1 stream:
 *   - P10A baseline: computed-goto C with all registers memory-mapped;
 *   - P10B: IP/SP/RP held in C locals (register-access idiom lowered);
 *   - P10C: P10B plus the pure arithmetic/comparison HOST ops lowered inline.
 *
 * This is a measurement experiment only; it changes no Glon/S1 semantics.
 */
#define _POSIX_C_SOURCE 200809L
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/stat.h>

#define CODE_BASE 256

static const char *FIB_DEF =
    "fib: func [n] [ either <= n 1 [ n ] [ + fib - n 1 fib - n 2 ] ]";
static const char *TREE_DEF =
    "t: func [n] [ either < n 2 [ n ] [ + t - n 1 t - n 2 ] ]";
static const char *RAW_DEF =
    "[ add2: raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD LIT 16 MUL ARITY 1 EXIT ] "
    " add2 3 4 ]";

static char prog[4096];

/* ============================ P10A emitter =============================== */

static void emit_host_p10a(FILE *f) {
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

static void emit_compiled_c_p10a(FILE *f, cell code_end) {
    cell n = code_end - CODE_BASE;
    unsigned char *instr = (unsigned char *)calloc((size_t)(n > 0 ? n : 1), 1);
    for (cell a = CODE_BASE; a < code_end; ) {
        instr[a - CODE_BASE] = 1;
        cell op = M[a];
        if (op == OP_LIT || op == OP_ZBRANCH || op == OP_HOST) a += 2; else a += 1;
    }
    fputs("#include <stdint.h>\n#include <stdio.h>\ntypedef intptr_t cell;\n"
          "static cell *M;\n\n", f);
    emit_host_p10a(f);
    fputs("void compiled_run(cell *vm, cell start) {\n    M = vm;\n"
          "    static void *const T[] = {\n", f);
    for (cell a = CODE_BASE; a < code_end; a++)
        if (instr[a - CODE_BASE]) fprintf(f, "        &&L_%ld,\n", (long)a);
        else fputs("        &&L_bad,\n", f);
    fputs("    };\n    M[0] = start;\n    goto *T[M[0] - 256];\n\n", f);
    for (cell a = CODE_BASE; a < code_end; ) {
        cell op = M[a];
        fprintf(f, "L_%ld:\n", (long)a);
        switch (op) {
        case OP_LIT:
            fprintf(f, "    M[--M[1]] = %ld;\n    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n",
                    (long)M[a+1], (long)(a+2)); a += 2; break;
        case OP_DUP:
            fputs("    { cell v = M[M[1]]; M[--M[1]] = v; }\n", f);
            fprintf(f, "    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n", (long)(a+1)); a += 1; break;
        case OP_DROP:
            fputs("    M[1]++;\n", f);
            fprintf(f, "    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n", (long)(a+1)); a += 1; break;
        case OP_FETCH:
            fputs("    { cell _a = M[M[1]++]; cell v = M[_a]; M[--M[1]] = v; }\n", f);
            fprintf(f, "    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n", (long)(a+1)); a += 1; break;
        case OP_STORE:
            fprintf(f, "    M[0] = %ld;\n", (long)(a+1));
            fputs("    { cell _a = M[M[1]++]; cell v = M[M[1]++]; M[_a] = v; }\n", f);
            fputs("    goto *T[M[0] - 256];\n\n", f); a += 1; break;
        case OP_ZBRANCH:
            fprintf(f, "    { cell f = M[M[1]++]; M[0] = (f == 0) ? %ld : %ld; }\n",
                    (long)M[a+1], (long)(a+2));
            fputs("    goto *T[M[0] - 256];\n\n", f); a += 2; break;
        case OP_HOST:
            fprintf(f, "    host(%ld);\n    M[0] = %ld;\n    goto *T[M[0] - 256];\n\n",
                    (long)M[a+1], (long)(a+2)); a += 2; break;
        case OP_HALT:
            fputs("    return;\n\n", f); a += 1; break;
        default:
            fputs("    return;\n\n", f); a += 1; break;
        }
    }
    fputs("L_bad:\n    return;\n}\n", f);
    free(instr);
}

/* ============================ P10B emitter =============================== */

static void emit_host_p10b(FILE *f) {
    fputs(
"static cell host(cell id, cell sp) {\n"
"    switch (id) {\n"
"    case 0:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = a + b; break; }\n"
"    case 1:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = a - b; break; }\n"
"    case 2:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = a * b; break; }\n"
"    case 3:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = a / b; break; }\n"
"    case 4:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = a % b; break; }\n"
"    case 5:  { cell v = M[sp++]; M[--sp] = -v; break; }\n"
"    case 6:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = (a == b) ? 1 : 0; break; }\n"
"    case 7:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = (a != b) ? 1 : 0; break; }\n"
"    case 8:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = (a <  b) ? 1 : 0; break; }\n"
"    case 9:  { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = (a >  b) ? 1 : 0; break; }\n"
"    case 10: { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = (a <= b) ? 1 : 0; break; }\n"
"    case 11: { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = (a >= b) ? 1 : 0; break; }\n"
"    case 12: { cell n = M[sp++]; cell a = M[3]; M[3] += n; M[--sp] = a; break; }\n"
"    case 13: { putchar((int)M[sp++]); fflush(stdout); break; }\n"
"    case 14: { printf(\"%ld\\n\", (long)M[sp++]); fflush(stdout); break; }\n"
"    case 15: { fprintf(stderr, \"[dump] IP=%ld SP=%ld RP=%ld HP=%ld top=%ld\\n\",\n"
"                    (long)M[0], (long)M[1], (long)M[2], (long)M[3], (long)M[M[1]]); break; }\n"
"    default: { fprintf(stderr, \"s1: bad host id %ld\\n\", (long)id); break; }\n"
"    }\n"
"    return sp;\n"
"}\n", f);
}

static void emit_compiled_c_p10b(FILE *f, cell code_end, int p10c) {
    cell n = code_end - CODE_BASE;
    unsigned char *instr = (unsigned char *)calloc((size_t)(n > 0 ? n : 1), 1);
    /* First pass: mark instruction starts, applying the same register-access
     * folding as the emission pass so the dispatch table matches the labels. */
    for (cell a = CODE_BASE; a < code_end; ) {
        instr[a - CODE_BASE] = 1;
        cell op = M[a];
        if (op == OP_LIT) {
            cell operand = M[a+1];
            cell next_op = (a + 2 < code_end) ? M[a+2] : -1;
            if (operand >= 0 && operand <= 2 &&
                (next_op == OP_FETCH || next_op == OP_STORE))
                a += 3;
            else
                a += 2;
        } else if (op == OP_ZBRANCH || op == OP_HOST) {
            a += 2;
        } else {
            a += 1;
        }
    }
    fputs("#include <stdint.h>\n#include <stdio.h>\ntypedef intptr_t cell;\n"
          "static cell *M;\n\n", f);
    emit_host_p10b(f);
    fputs("void compiled_run(cell *vm, cell start) {\n    M = vm;\n"
          "    cell ip, sp, rp;\n    ip = M[0]; sp = M[1]; rp = M[2];\n"
          "    static void *const T[] = {\n", f);
    for (cell a = CODE_BASE; a < code_end; a++)
        if (instr[a - CODE_BASE]) fprintf(f, "        &&L_%ld,\n", (long)a);
        else fputs("        &&L_bad,\n", f);
    fputs("    };\n    ip = start;\n    goto *T[ip - 256];\n\n", f);
    for (cell a = CODE_BASE; a < code_end; ) {
        cell op = M[a];
        fprintf(f, "L_%ld:\n", (long)a);
        switch (op) {
        case OP_LIT: {
            cell operand = M[a+1];
            cell next_op = (a + 2 < code_end) ? M[a+2] : -1;
            if (operand >= 0 && operand <= 2 &&
                (next_op == OP_FETCH || next_op == OP_STORE)) {
                const char *reg = (operand == 0) ? "ip" : (operand == 1) ? "sp" : "rp";
                if (next_op == OP_FETCH) {
                    /* read register: push the local, then fall through.
                     * For SP the local is the stack pointer itself, so read it
                     * into a temporary to avoid the unsequenced M[--sp]=sp. */
                    if (operand == 1)
                        fputs("    { cell _s = sp; M[--sp] = _s; }\n", f);
                    else
                        fprintf(f, "    M[--sp] = %s;\n", reg);
                    fprintf(f, "    ip = %ld;\n    goto *T[ip - 256];\n\n", (long)(a + 3));
                } else if (operand == 0) {
                    /* write IP: pop into ip (a branch), dispatch on the new ip */
                    fputs("    ip = M[sp++];\n    goto *T[ip - 256];\n\n", f);
                } else if (operand == 1) {
                    /* write SP: pop the value into sp (read M[sp] before the
                     * assignment -- sp = M[sp] is well-defined) */
                    fputs("    sp = M[sp];\n", f);
                    fprintf(f, "    ip = %ld;\n    goto *T[ip - 256];\n\n", (long)(a + 3));
                } else {
                    /* write RP: pop into rp, then fall through */
                    fputs("    rp = M[sp++];\n", f);
                    fprintf(f, "    ip = %ld;\n    goto *T[ip - 256];\n\n", (long)(a + 3));
                }
                a += 3;
            } else {
                fprintf(f, "    M[--sp] = %ld;\n    ip = %ld;\n    goto *T[ip - 256];\n\n",
                        (long)operand, (long)(a + 2));
                a += 2;
            }
            break;
        }
        case OP_DUP:
            fputs("    { cell v = M[sp]; M[--sp] = v; }\n", f);
            fprintf(f, "    ip = %ld;\n    goto *T[ip - 256];\n\n", (long)(a+1)); a += 1; break;
        case OP_DROP:
            fputs("    sp++;\n", f);
            fprintf(f, "    ip = %ld;\n    goto *T[ip - 256];\n\n", (long)(a+1)); a += 1; break;
        case OP_FETCH:
            fputs("    { cell _a = M[sp++]; M[--sp] = M[_a]; }\n", f);
            fprintf(f, "    ip = %ld;\n    goto *T[ip - 256];\n\n", (long)(a+1)); a += 1; break;
        case OP_STORE:
            fputs("    { cell _a = M[sp++]; cell v = M[sp++]; M[_a] = v; }\n", f);
            fprintf(f, "    ip = %ld;\n    goto *T[ip - 256];\n\n", (long)(a+1)); a += 1; break;
        case OP_ZBRANCH:
            fprintf(f, "    { cell f = M[sp++]; ip = (f == 0) ? %ld : %ld; }\n",
                    (long)M[a+1], (long)(a+2));
            fputs("    goto *T[ip - 256];\n\n", f); a += 2; break;
        case OP_HOST: {
            cell id = M[a+1];
            if (id == HOST_DUMP) {
                fputs("    M[0] = ip; M[1] = sp; M[2] = rp;\n", f);
                fprintf(f, "    sp = host(%ld, sp);\n", (long)id);
                fputs("    ip = M[0]; sp = M[1]; rp = M[2];\n", f);
            } else if (p10c && id >= 0 && id <= 11) {
                /* P10C: pure deterministic arithmetic/comparison, lowered inline.
                 * Each is a verbatim transcription of the frozen host() switch
                 * case, so semantics (signed arithmetic, truncation, comparison
                 * representation) are identical. */
                static const char *arith[6] = { "a + b", "a - b", "a * b", "a / b", "a % b", "-v" };
                static const char *cmp[6] = { "a == b", "a != b", "a < b", "a > b", "a <= b", "a >= b" };
                if (id <= 4)
                    fprintf(f, "    { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = %s; }\n", arith[id]);
                else if (id == 5)
                    fputs("    { cell v = M[sp++]; M[--sp] = -v; }\n", f);
                else
                    fprintf(f, "    { cell b = M[sp++]; cell a = M[sp++]; M[--sp] = (%s) ? 1 : 0; }\n", cmp[id - 6]);
            } else {
                fprintf(f, "    sp = host(%ld, sp);\n", (long)id);
            }
            fprintf(f, "    ip = %ld;\n    goto *T[ip - 256];\n\n", (long)(a+2)); a += 2; break;
        }
        case OP_HALT:
            fputs("    M[0] = ip; M[1] = sp; M[2] = rp;\n    return;\n\n", f); a += 1; break;
        default:
            fputs("    M[0] = ip; M[1] = sp; M[2] = rp;\n    return;\n\n", f); a += 1; break;
        }
    }
    fputs("L_bad:\n    M[0] = ip; M[1] = sp; M[2] = rp;\n    return;\n}\n", f);
    free(instr);
}

/* ============================ driver ===================================== */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int build_so(const char *cpath, const char *sopath, cell code_end, int mode) {
    /* mode: 0 = P10A, 1 = P10B, 2 = P10C */
    struct stat src_st, so_st;
    int need = 1;
    if (stat(cpath, &src_st) == 0 && stat(sopath, &so_st) == 0 &&
        so_st.st_mtime >= src_st.st_mtime)
        need = 0;
    if (!need) return 0;

    FILE *cf = fopen(cpath, "w");
    if (!cf) return -1;
    if (mode == 0)      emit_compiled_c_p10a(cf, code_end);
    else                emit_compiled_c_p10b(cf, code_end, mode == 2);
    fclose(cf);

    char cmd[512];
    snprintf(cmd, sizeof cmd, "gcc -O2 -shared -fPIC %s -o %s", cpath, sopath);
    return system(cmd);
}

static void *load_so(const char *sopath) {
    void *h = dlopen(sopath, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", sopath, dlerror()); exit(1); }
    void (*fn)(cell *, cell) = (void (*)(cell *, cell))dlsym(h, "compiled_run");
    if (!fn) { fprintf(stderr, "dlsym: %s\n", dlerror()); exit(1); }
    return (void *)fn;
}

int main(void) {
    r0_s1_init();

    int err;
    snprintf(prog, sizeof prog, "[ %s fib 25 ]", FIB_DEF);
    cell fib_block = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "fib parse err\n"); return 1; }
    snprintf(prog, sizeof prog, "[ %s t 25 ]", TREE_DEF);
    cell tree_block = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "tree parse err\n"); return 1; }
    snprintf(prog, sizeof prog, "%s", RAW_DEF);
    cell raw_block = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "raw parse err\n"); return 1; }

    cell code_end = asm_here();

    double c0 = now_sec();
    if (build_so("/tmp/opencode_p10b.c", "/tmp/opencode_p10b.so", code_end, 1) != 0) {
        fprintf(stderr, "P10B build failed\n"); return 1;
    }
    if (build_so("/tmp/opencode_p10c.c", "/tmp/opencode_p10c.so", code_end, 2) != 0) {
        fprintf(stderr, "P10C build failed\n"); return 1;
    }
    double c1 = now_sec();
    printf("build time: %.3f s (code cells %ld)\n", c1 - c0, (long)(code_end - CODE_BASE));

    void (*run_p10b)(cell *, cell) = (void (*)(cell *, cell))load_so("/tmp/opencode_p10b.so");
    void (*run_p10c)(cell *, cell) = (void (*)(cell *, cell))load_so("/tmp/opencode_p10c.so");

    /* correctness: P10B and P10C must equal interpreted/P10A on all workloads. */
    {
        cell blocks[7];
        const char *names[7];
        blocks[0] = fib_block;  names[0] = "fib 25";
        blocks[1] = tree_block; names[1] = "tree 25";
        blocks[2] = raw_block;  names[2] = "raw add2";
        snprintf(prog, sizeof prog, "[ f: func [] [ return 42  99 ]  f ]");
        blocks[3] = r0_s1_parse(prog, &err); names[3] = "non-local return";
        snprintf(prog, sizeof prog, "[ make: func [n] [ func [] [ n ] ]  g: make 5  g ]");
        blocks[4] = r0_s1_parse(prog, &err); names[4] = "closure capture";
        /* arithmetic edge cases (signed division, negative, equality, comparison) */
        snprintf(prog, sizeof prog,
            "[ values [ = + 5 3 8   = - 5 3 2   = * 5 3 15   = / 7 2 3 "
            "  = / -7 2 -3   = 5 5   < 1 2   > 2 1   <= 2 2   >= 2 2 ] ]");
        blocks[5] = r0_s1_parse(prog, &err); names[5] = "arith edge cases";
        snprintf(prog, sizeof prog,
            "[ values [ + -1 1   - 0 1   * 3 -4   = < 0 -1 1 ] ]");
        blocks[6] = r0_s1_parse(prog, &err); names[6] = "arith negative";
        int ok = 1;
        for (int i = 0; i < 7; i++) {
            int Ni = r0_s1_run(blocks[i]);
            cell Ri = (Ni == 1) ? r0_s1_result(0, Ni) : -999;
            int Nb = r0_s1_run_compiled(blocks[i], run_p10b);
            cell Rb = (Nb == 1) ? r0_s1_result(0, Nb) : -999;
            int Nc = r0_s1_run_compiled(blocks[i], run_p10c);
            cell Rc = (Nc == 1) ? r0_s1_result(0, Nc) : -999;
            int match = (Ni == Nb && Ri == Rb && Ni == Nc && Ri == Rc);
            printf("  %s: interp N=%d  P10B N=%d r=%ld  P10C N=%d r=%ld  %s\n",
                   names[i], Ni, Nb, (long)Rb, Nc, (long)Rc, match ? "MATCH" : "MISMATCH");
            if (!match) ok = 0;
        }
        if (!ok) { fprintf(stderr, "CORRECTNESS MISMATCH\n"); return 1; }
    }

    r0_s1_run_compiled(fib_block, run_p10b);
    r0_s1_run_compiled(fib_block, run_p10c);

    static double tb[8], tc[8];
    int got = 0;
    for (int r = 0; r < 8; r++) {
        double t0 = now_sec(); r0_s1_run_compiled(fib_block, run_p10b); double t1 = now_sec();
        tb[got] = t1 - t0;
        t0 = now_sec(); r0_s1_run_compiled(fib_block, run_p10c); t1 = now_sec();
        tc[got] = t1 - t0;
        got++;
    }
    for (int i = 0; i < got; i++)
        for (int j = i + 1; j < got; j++) {
            if (tb[j] < tb[i]) { double t = tb[i]; tb[i] = tb[j]; tb[j] = t; }
            if (tc[j] < tc[i]) { double t = tc[i]; tc[i] = tc[j]; tc[j] = t; }
        }
    double mb = tb[got/2], mc = tc[got/2];
    printf("fib 25 P10B (promoted): median %.4f s (min %.4f, max %.4f)\n", mb, tb[0], tb[got-1]);
    printf("fib 25 P10C (HOST intrin): median %.4f s (min %.4f, max %.4f)\n", mc, tc[0], tc[got-1]);
    printf("speedup (P10B/P10C): %.3fx\n", mb / mc);

    return 0;
}
