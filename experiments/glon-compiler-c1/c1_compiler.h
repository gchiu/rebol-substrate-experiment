#ifndef C1_COMPILER_H
#define C1_COMPILER_H
#include "b3_compiler.h"
#include <stddef.h>

/* Compile the B3 textual subset plus block-at. If function is non-NULL,
 * compile all definitions and select that function as the entry; a top-level
 * expression is optional. This is the C1 vertical-slice compiler. */
int c1_compile(const char *source, const char *function, unsigned flags,
               b3_convention conv, b3_code *out, char *err, size_t errlen);
#endif
