#ifndef B4_DIRECT_H
#define B4_DIRECT_H

#include "b4_common.h"
#include <stddef.h>

/* C: direct machine-oriented 32-bit words, little-endian.
 * word = (op << 24) | operand, except magic 0x43344231. */
#define B4C_MAGIC 0x43344231u
enum {
    BC_ENTRY = 1,   /* operand = entry label id */
    BC_LABEL,       /* operand = label id */
    BC_LIT,         /* operand = raw S1 cell */
    BC_DUP,
    BC_DROP,
    BC_FETCH,
    BC_STORE,
    BC_ZBRANCH,     /* operand = label id */
    BC_HOST,        /* operand = raw HOST id */
    BC_CALL,        /* operand = label id; native CALL */
    BC_RET,         /* native RET */
    BC_TO_R,        /* native >R */
    BC_FROM_R,      /* native R> */
    BC_R_FETCH,     /* native R@ */
    BC_BRANCH,      /* operand = label id; native BRANCH */
    BC_HALT
};

int b4_direct_assemble(const unsigned char *data, size_t n,
                       b4_code *out, char *err, size_t errlen);

#endif
