#ifndef B3_COMPILER_H
#define B3_COMPILER_H

#include "s1.h"
#include <stddef.h>

/* B2 target features. Each bit selects a native replacement for the matching
 * B1 macro form. When a bit is clear the compiler emits the exact B1 code. */
enum {
    B3_NATIVE_CALL   = 1u << 0,
    B3_NATIVE_RET    = 1u << 1,
    B3_NATIVE_TO_R   = 1u << 2,   /* >R */
    B3_NATIVE_FROM_R = 1u << 3,   /* R> */
    B3_NATIVE_RFETCH = 1u << 4,   /* R@ */
    B3_NATIVE_BRANCH = 1u << 5
};

typedef struct {
    unsigned flags;
} b3_target;

typedef enum {
    B3_CONV_RETSTACK = 0,   /* B1/B2: parameters are saved on the return stack */
    B3_CONV_DATAARGS = 1    /* B3-B: parameters stay on the data stack */
} b3_convention;

typedef struct {
    cell entry;
    long code_start;
    long code_end;
    long code_cells;
    int nfuncs;
    unsigned flags;
    b3_convention conv;
} b3_code;

/* Compile the B1 source subset to either B1 macros (flags == 0) or to the
 * selected B2 native forms, under the selected calling convention. */
int b3_compile(const char *source, unsigned flags, b3_convention conv,
               b3_code *out, char *err, size_t errlen);

#endif
