/* r0_s1_m3d_lib.h - M3D managed BLOCK! library (RAW mechanics + GLON policy).
 *
 * Managed BLOCK! is an immutable tagged-value series: T_BLOCK (reused tag 6)
 * over a managed GC_KIND_BLOCK object with payload [length, value0..valueN-1].
 * Elements are ordinary tagged R0 values and are traced generically by GC.
 *
 * This header is NOT part of the frozen substrate; it is M3D's driver-level
 * library. It is loaded AFTER the M3C string library, so its `append` (a
 * functional *value* append) shadows the string library's byte append; `length?`
 * and `copy` are reused unchanged (they are already generic over p+0-length /
 * identity).
 */
#ifndef R0_S1_M3D_LIB_H
#define R0_S1_M3D_LIB_H

#include "r0_s1.h"
#include <stdio.h>

static inline const char *m3d_block_lib(char *buf, size_t n, cell alloc_addr) {
    snprintf(buf, n,
        /* --- generic RAW mechanics (trusted) ---
         * blk-alloc ( raw-N -> managed empty block )
         * blk-at    ( block raw-i -> element )
         * blk-set   ( block raw-i value -> block )   construction write only
         * mk-block  ( loader-block -> managed block ) copy source elements verbatim */
        " blk-alloc: raw 1 [ LIT SCRATCH_A ! LIT SCRATCH_A @ LIT 1 ADD LIT 15 ADD LIT 16 DIV LIT 16 MUL LIT GC_KIND_BLOCK CALL %ld LIT SCRATCH_B ! LIT SCRATCH_A @ LIT SCRATCH_B @ ! LIT SCRATCH_B @ LIT T_BLOCK ADD ARITY 1 EXIT ] "
        " blk-at: raw 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 1 ADD LIT SCRATCH_B @ ADD @ ARITY 1 EXIT ] "
        " blk-set: raw 3 [ LIT SCRATCH_C ! LIT SCRATCH_B ! LIT SCRATCH_A ! LIT SCRATCH_C @ LIT SCRATCH_A @ DUP LIT 16 MOD SUB LIT 1 ADD LIT SCRATCH_B @ ADD ! LIT SCRATCH_A @ ARITY 1 EXIT ] "
        " mk-block: raw 1 [ "
        "   LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ @ LIT SCRATCH_C ! "
        "   LIT SCRATCH_C @ LIT 1 ADD LIT 15 ADD LIT 16 DIV LIT 16 MUL "
        "   LIT GC_KIND_BLOCK CALL %ld LIT SCRATCH_E ! "
        "   LIT SCRATCH_C @ LIT SCRATCH_E @ ! "
        "   LIT 0 LIT SCRATCH_D ! "
        " Lmbl: LIT SCRATCH_D @ LIT SCRATCH_C @ GE ZBRANCH Lmbf "
        "   BRANCH Lmbd "
        " Lmbf: "
        "   LIT SCRATCH_A @ LIT 2 ADD LIT SCRATCH_D @ ADD @ "
        "   LIT SCRATCH_E @ LIT 1 ADD LIT SCRATCH_D @ ADD ! "
        "   LIT SCRATCH_D @ LIT 1 ADD LIT SCRATCH_D ! BRANCH Lmbl "
        " Lmbd: LIT SCRATCH_E @ LIT T_BLOCK ADD ARITY 1 EXIT ] "
        /* --- GLON policy --- */
        " append: func [blk v] [ "
        "   n: length? blk "
        "   r: blk-alloc div16 + n 1 "
        "   blk-copy r blk 0 n "
        "   blk-set r div16 n v "
        "   r ] "
        " blk-copy: func [r b i n] [ "
        "   either < i n [ blk-set r div16 i blk-at b div16 i blk-copy r b + i 1 n ] [ none ] ] "
        " block-at: func [b i] [ either < i length? b [ blk-at b div16 i ] [ reject ] ] "
        " block=?: func [a b] [ "
        "   either = length? a length? b [ blk-cmp a b 0 length? a ] [ 0 ] ] "
        " blk-cmp: func [a b i n] [ "
        "   either < i n [ either = block-at a i block-at b i [ blk-cmp a b + i 1 n ] [ 0 ] ] [ 1 ] ] ",
        (long)alloc_addr, (long)alloc_addr);
    return buf;
}

#endif /* R0_S1_M3D_LIB_H */
