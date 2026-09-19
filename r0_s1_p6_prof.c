/* r0_s1_p6_prof.c - P6 diagnostic driver: run naive fib and report both the
 * evaluator event counters (PF_*) and the S1 opcode/HOST dispatch counters.
 * Counts only, no per-operation timing. Uses the instrumented s1_prof.c. */
#define _POSIX_C_SOURCE 199309L
#include "r0_s1.h"
#include "s1_prof.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FIB_DEF =
    "fib: func [n] [ either <= n 1 [ n ] [ + fib - n 1 fib - n 2 ] ]";

static char prog[4096];

int main(int argc, char **argv) {
    int n = 25;
    if (argc > 1) n = atoi(argv[1]);

    r0_s1_init();
    snprintf(prog, sizeof prog, "[ %s fib %d ]", FIB_DEF, n);
    int err = 0;
    cell b = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "parse error\n"); return 1; }

    s1_prof_reset();
    int N = r0_s1_run(b);

    r0_s1_pf_stats s;
    r0_s1_pf_read(&s);

    long total = 0;
    for (int i = 0; i < 8; i++) total += s1_prof_op_count[i];

    printf("fib %d: N=%d result=%ld\n", n, N, (long)(N == 1 ? int_val(r0_s1_result(0,1)) : -1));
    printf("total S1 instructions: %ld\n", total);
    printf("opcode counts:\n");
    static const char *opnames[8] = {"LIT","DUP","DROP","@","!","0BRANCH","HOST","HALT"};
    for (int i = 0; i < 8; i++)
        printf("  %-8s %10ld  (%5.2f%%)\n", opnames[i], s1_prof_op_count[i],
               total ? 100.0 * s1_prof_op_count[i] / total : 0.0);

    long host_total = 0;
    for (int i = 0; i < 16; i++) host_total += s1_prof_host_count[i];
    printf("HOST total calls: %ld\n", host_total);
    static const char *hostnames[16] = {
        "ADD","SUB","MUL","DIV","MOD","NEG","EQ","NE","LT","GT","LE","GE",
        "ALLOC","PUTCHAR","PRINT","DUMP"};
    for (int i = 0; i < 16; i++)
        if (s1_prof_host_count[i])
            printf("  %-8s %10ld\n", hostnames[i], s1_prof_host_count[i]);

    printf("evaluator events:\n");
    printf("  calls=%ld  subexpr=%ld  blkeval=%ld  lookups=%ld  lex-direct=%ld\n",
           (long)s.closure, (long)s.subexpr, (long)s.blkeval, (long)s.lookup, (long)s.lex_direct);
    printf("  hash-probes=%ld  hits=%ld  misses=%ld  collisions=%ld  fallback=%ld\n",
           (long)s.hash_probes, (long)s.hash_hits, (long)s.hash_misses,
           (long)s.hash_collisions, (long)s.hash_fallback);
    printf("  natives=%ld (<=%ld -%ld +%ld either%ld other%ld)\n",
           (long)s.native, (long)s.nat_le, (long)s.nat_sub, (long)s.nat_add,
           (long)s.nat_either, (long)s.nat_other);
    printf("  allocs=%ld  max-depth=%ld\n", (long)s.allocs, (long)s.max_depth);
    printf("  HOST-calls(arithmetic natives, PF counter)=%ld\n", (long)r0_s1_host_calls());

    printf("amplification:\n");
    printf("  S1 instructions / fib call = %.2f\n", s.closure ? (double)total / s.closure : 0.0);
    printf("  S1 instructions / subexpression = %.2f\n", s.subexpr ? (double)total / s.subexpr : 0.0);
    printf("  HOST calls / fib call = %.2f\n", s.closure ? (double)host_total / s.closure : 0.0);
    printf("cycle attribution (rdtsc):\n");
    printf("  s1_run total cycles = %llu\n", s1_prof_run_cycles);
    printf("  host total cycles   = %llu\n", s1_prof_host_cycles);
    if (s1_prof_run_cycles)
        printf("  host share = %.1f%%   dispatch+memory share = %.1f%%\n",
               100.0 * s1_prof_host_cycles / s1_prof_run_cycles,
               100.0 * (s1_prof_run_cycles - s1_prof_host_cycles) / s1_prof_run_cycles);
    return 0;
}
