#ifndef B2_COMPILER_H
#define B2_COMPILER_H

#include "s1.h"
#include <stddef.h>

/* B2 target features. Each bit selects a native replacement for the matching
 * B1 macro form. When a bit is clear the compiler emits the exact B1 code. */
enum {
    B2_NATIVE_CALL   = 1u << 0,
    B2_NATIVE_RET    = 1u << 1,
    B2_NATIVE_TO_R   = 1u << 2,   /* >R */
    B2_NATIVE_FROM_R = 1u << 3,   /* R> */
    B2_NATIVE_RFETCH = 1u << 4,   /* R@ */
    B2_NATIVE_BRANCH = 1u << 5
};

typedef struct {
    unsigned flags;
} b2_target;

typedef struct {
    cell entry;
    long code_start;
    long code_end;
    long code_cells;
    int nfuncs;
    unsigned flags;
} b2_code;

/* Compile the B1 source subset to either B1 macros (flags == 0) or to the
 * selected B2 native forms. */
int b2_compile(const char *source, unsigned flags, b2_code *out,
               char *err, size_t errlen);

#endif
