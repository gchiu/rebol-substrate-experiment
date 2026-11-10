#ifndef B1_S1_COUNT_H
#define B1_S1_COUNT_H

/* Counter interface for the B1 measurement copy of frozen s1.c. */
extern long b1_insns;
extern long b1_min_sp;
extern long b1_min_rp;
void b1_reset_counts(void);

#endif
