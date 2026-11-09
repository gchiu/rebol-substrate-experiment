/* integrity/asan_probe.c - the masm label/reference tables, under
 * AddressSanitizer (built and run only by `make machine-integrity`).
 *
 * The assembler records a block's labels in raw_labels[64] and its label
 * references in raw_refs[128] (r0_s1_runtime.c), while a block may hold up to
 * 511 tokens. Until the K5 repair (2026-11-10) neither was bounds-checked: a
 * block with 65 labels or 129 references wrote past a C array. The parser now
 * counts both and refuses such a block as too_large before emitting anything.
 * This probe runs ONE block per process in an AddressSanitizer build, so a
 * regression is caught at the first out-of-bounds access.
 *
 *   asan-probe LABELS REFS [call]   REFS are `ZBRANCH L0`, or `CALL L0` with
 *                                   "call"; prints the parse outcome and the
 *                                   cells emitted
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char src[1 << 16];

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: asan-probe LABELS REFS [call]\n"); return 2; }
    int nl = atoi(argv[1]), nr = atoi(argv[2]);
    const char *ref = (argc > 3 && !strcmp(argv[3], "call")) ? "CALL L0 " : "ZBRANCH L0 ";
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    /* labels: L0: L1: ...; references to L0 (assembled, never run) */
    int n = snprintf(src, sizeof src, "[ w: masm 0 [ ");
    for (int i = 0; i < nl; i++) n += snprintf(src + n, sizeof src - (size_t)n, "L%d: ", i);
    for (int i = 0; i < nr; i++) n += snprintf(src + n, sizeof src - (size_t)n, "%s", ref);
    snprintf(src + n, sizeof src - (size_t)n, "LIT 7 ARITY 1 EXIT ] 0 ]");
    if (nl == 0) { /* a reference needs its label: define L0 after them */
        char *tail = strstr(src, "LIT 7 ARITY");
        memmove(tail + 4, tail, strlen(tail) + 1); memcpy(tail, "L0: ", 4);
    }
    int err = 0;
    cell before = asm_here();
    r0_s1_parse(src, &err);
    printf("asan-probe: %d labels, %d references: %s, %ld cells emitted\n", nl, nr,
           err ? (r0_s1_parse_error_kind() == R0S1_PARSE_TOO_LARGE ? "refused (too_large)" : "refused (other)")
               : "assembled", (long)(asm_here() - before));
    return 0;
}
