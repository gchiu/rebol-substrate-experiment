/* integrity/asan_probe.c - the masm label/reference tables, under
 * AddressSanitizer (built and run only by `make machine-integrity`).
 *
 * The assembler records a block's labels in raw_labels[64] and its forward
 * references in raw_refs[128] (r0_s1_runtime.c) without a bound check, while
 * a block may hold up to 510 tokens. A block with 65 labels or 129 references
 * therefore writes past a C array: host undefined behaviour, which the
 * ordinary test binaries must never execute. This probe runs ONE such block
 * per process, in an AddressSanitizer build, so the write is caught at the
 * first out-of-bounds cell and the process stops there.
 *
 *   asan-probe LABELS REFS     prints the parse outcome; an overflow is
 *                              reported by ASan (exit status non-zero)
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char src[1 << 16];

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: asan-probe LABELS REFS\n"); return 2; }
    int nl = atoi(argv[1]), nr = atoi(argv[2]);
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    /* labels: L0: L1: ...; references: ZBRANCH L0 (assembled, never run) */
    int n = snprintf(src, sizeof src, "[ w: masm 0 [ ");
    for (int i = 0; i < nl; i++) n += snprintf(src + n, sizeof src - (size_t)n, "L%d: ", i);
    for (int i = 0; i < nr; i++) n += snprintf(src + n, sizeof src - (size_t)n, "ZBRANCH L0 ");
    snprintf(src + n, sizeof src - (size_t)n, "LIT 7 ARITY 1 EXIT ] 0 ]");
    int err = 0;
    r0_s1_parse(src, &err);
    printf("asan-probe: %d labels, %d references: %s\n", nl, nr,
           err ? "refused at parse" : "assembled");
    return 0;
}
