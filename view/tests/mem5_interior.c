/* GLON-MEM-5: the root GC interior tests (r0_s1_gc_interior_tests.c) in a
 * view memory profile (GLON-VIEW-CORE-V3: a C2 generated profile, force-included;
 * formerly the experiments/ms1 alternate header,
 * first on the include path); the included test file's own "m1_layout.h" is
 * the root one, as for the runtime itself. */
#include "r0_s1.h"
#include "../../r0_s1_gc_interior_tests.c"
int main(void) { return run_r0_s1_gc_interior_tests() ? 1 : 0; }
