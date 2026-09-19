/* s1_prof.h - P6 diagnostic counters for the S1 interpreter (NOT the frozen
 * substrate). This header is used only by the profiling build, which links an
 * instrumented copy of the S1 machine (s1_prof.c) instead of the frozen s1.c.
 * The counters are event counts only -- no per-operation timing -- so they do
 * not distort elapsed-time attribution. */
#ifndef S1_PROF_H
#define S1_PROF_H

#include "s1.h"

/* dynamic opcode dispatch counts, indexed by the frozen opcode ids (0..7) */
extern long s1_prof_op_count[8];

/* dynamic HOST service counts, indexed by the frozen HOST ids (0..15) */
extern long s1_prof_host_count[16];

/* dynamic code-location histogram: count of opcode executions per 256-cell
 * code bucket (IP>>8). Used to attribute execution to emitter functions. */
extern long s1_prof_ip_hist[256];

/* aggregate cycle accumulators (rdtsc) for coarse time attribution: total
 * inside s1_run, and inside the host() dispatch. Aggregate, not per-op. */
extern unsigned long long s1_prof_run_cycles;
extern unsigned long long s1_prof_host_cycles;

void s1_prof_reset(void);   /* zero the counters */

#endif /* S1_PROF_H */
