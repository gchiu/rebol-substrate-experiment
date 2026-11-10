#ifndef B1_COMPILER_H
#define B1_COMPILER_H

#include "s1.h"
#include <stddef.h>

typedef struct {
    cell entry;
    long code_start;
    long code_end;
    long code_cells;
    int nfuncs;
} b1_code;

/* Compile a small Glon subset to S1 code at the frozen assembler cursor.
 * On success, out->entry is the top-level entry. On failure, returns 0 and
 * writes a reason to err. */
int b1_compile(const char *source, b1_code *out, char *err, size_t errlen);

#endif
