#ifndef B4_COMMON_H
#define B4_COMMON_H

#include "b3_compiler.h"
#include "s1.h"

/* The common B3 target: convention A, all six native forms. */
#define B4_FLAGS (B3_NATIVE_CALL | B3_NATIVE_RET | B3_NATIVE_TO_R | \
                  B3_NATIVE_FROM_R | B3_NATIVE_RFETCH | B3_NATIVE_BRANCH)

typedef struct {
    cell entry;
    long code_cells;
} b4_code;

#endif
