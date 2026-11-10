#ifndef B4_TAGGED_H
#define B4_TAGGED_H

#include "b4_common.h"
#include <stddef.h>

/* B: semantically tagged 32-bit words, little-endian.
 * word = (role << 24) | operand, except the magic word 0x42344231. */
#define B4B_MAGIC 0x42344231u
enum {
    BT_FUNC = 1,       /* operand = (func_id << 8) | arity */
    BT_SEQ_BEGIN,      /* function body / top-level sequence */
    BT_SEQ_END,
    BT_TOP_BEGIN,
    BT_TOP_END,
    BT_INT,            /* signed 24-bit value */
    BT_PARAM,          /* zero-based parameter index */
    BT_CALL,           /* operand = (func_id << 8) | arity */
    BT_BIN,            /* 0 +, 1 -, 2 *, 3 <, 4 <=, 5 =, 6 >, 7 >= */
    BT_EITHER,
    BT_THEN_BEGIN,
    BT_THEN_END,
    BT_ELSE_BEGIN,
    BT_ELSE_END
};

int b4_tagged_compile(const unsigned char *data, size_t n,
                      b4_code *out, char *err, size_t errlen);

#endif
