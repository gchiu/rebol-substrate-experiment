/* r0_s1_m3c_lib.h - M3C STRING! library (generic RAW mechanics + GLON policy).
 *
 * STRING! is an intrinsic tag (T_STRING=12) over a managed, immutable byte
 * series (GC_KIND_STRING=6): payload [length, byte0..byteN-1] with one raw byte
 * (0..255) per cell. This library provides the construction/read/append/equality
 * operations, layered exactly like M3A's datatype library: generic mechanics in
 * RAW (trusted), policy in GLON.
 *
 * This header is NOT part of the frozen substrate; it is M3C's driver-level
 * library, shared by the M3C test suite and the M3B genealogy demo (which now
 * uses string! names).
 */
#ifndef R0_S1_M3C_LIB_H
#define R0_S1_M3C_LIB_H

#include "r0_s1.h"
#include <stdio.h>

/* Returns GLON source defining the STRING! operations, with the managed-heap
 * allocator entry address formatted in. */
static inline const char *m3c_string_lib(char *buf, size_t n, cell alloc_addr) {
    snprintf(buf, n,
        /* integer representation helpers (raw <-> tagged) */
        " tag-int: raw 1 [ LIT 16 MUL ARITY 1 EXIT ] "
        " div16: raw 1 [ LIT 16 DIV ARITY 1 EXIT ] "
        /* --- generic RAW mechanics (trusted) ---
         * str-len  ( string -> raw N )
         * str-byte ( string raw-i -> raw byte )
         * str-fill ( string raw-i raw-byte -> string )  construction write only
         * str-alloc ( raw-N -> string )
         * NOTE: "DUP LIT 16 MOD SUB" leaves only the untagged pointer (the
         * original value is consumed), so these mirror block-len/block-pick
         * exactly and leave exactly one result + the tagged count. */
        " str-len: raw 1 [ DUP LIT 16 MOD SUB @ ARITY 1 EXIT ] "
        " str-byte: raw 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 1 ADD LIT SCRATCH_B @ ADD @ ARITY 1 EXIT ] "
        " str-fill: raw 3 [ LIT SCRATCH_C ! LIT SCRATCH_B ! LIT SCRATCH_A ! LIT SCRATCH_C @ LIT SCRATCH_A @ DUP LIT 16 MOD SUB LIT 1 ADD LIT SCRATCH_B @ ADD ! LIT SCRATCH_A @ ARITY 1 EXIT ] "
        " str-alloc: raw 1 [ LIT SCRATCH_A ! LIT SCRATCH_A @ LIT 1 ADD LIT 15 ADD LIT 16 DIV LIT 16 MUL LIT GC_KIND_STRING CALL %ld LIT SCRATCH_B ! LIT SCRATCH_A @ LIT SCRATCH_B @ ! LIT SCRATCH_B @ LIT T_STRING ADD ARITY 1 EXIT ] "
        /* mk-string: ( block-of-integers -> string | 0 results ). Validates each
         * input is integer! and 0..255, then allocates exact storage, writes
         * length and bytes, and returns one STRING result. */
        " mk-string: raw 1 [ "
        "   LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ @ LIT SCRATCH_C ! "
        "   LIT 0 LIT SCRATCH_D ! "
        " Lmsval: LIT SCRATCH_D @ LIT SCRATCH_C @ GE ZBRANCH Lmsfield "
        "   BRANCH Lmsok "
        " Lmsfield: "
        "   LIT SCRATCH_A @ LIT 2 ADD LIT SCRATCH_D @ ADD @ LIT SCRATCH_E ! "
        "   LIT SCRATCH_E @ LIT 16 MOD LIT 0 EQ ZBRANCH Lmsbad "
        "   LIT SCRATCH_E @ LIT 16 DIV LIT SCRATCH_E ! "
        "   LIT SCRATCH_E @ LIT 0 LT ZBRANCH Lmsr1 BRANCH Lmsbad "
        " Lmsr1: LIT SCRATCH_E @ LIT 255 GT ZBRANCH Lmsr2 BRANCH Lmsbad "
        " Lmsr2: LIT SCRATCH_D @ LIT 1 ADD LIT SCRATCH_D ! BRANCH Lmsval "
        " Lmsok: "
        "   LIT SCRATCH_C @ LIT 1 ADD LIT 15 ADD LIT 16 DIV LIT 16 MUL "
        "   LIT GC_KIND_STRING CALL %ld LIT SCRATCH_E ! "
        "   LIT SCRATCH_C @ LIT SCRATCH_E @ ! "
        "   LIT 0 LIT SCRATCH_D ! "
        " Lmsfil: LIT SCRATCH_D @ LIT SCRATCH_C @ GE ZBRANCH Lmsf2 "
        "   BRANCH Lmsdone "
        " Lmsf2: "
        "   LIT SCRATCH_A @ LIT 2 ADD LIT SCRATCH_D @ ADD @ LIT 16 DIV "
        "   LIT SCRATCH_E @ LIT 1 ADD LIT SCRATCH_D @ ADD ! "
        "   LIT SCRATCH_D @ LIT 1 ADD LIT SCRATCH_D ! BRANCH Lmsfil "
        " Lmsdone: LIT SCRATCH_E @ LIT T_STRING ADD ARITY 1 EXIT "
        " Lmsbad: ARITY 0 EXIT ] "
        /* --- GLON policy --- */
        " string?: func [x] [ = type? x string! ] "
        " length?: func [x] [ tag-int str-len x ] "
        " string-byte: func [s i] [ tag-int str-byte s div16 i ] "
        " append: func [s b] [ "
        "   n: length? s "
        "   r: str-alloc div16 + n 1 "
        "   append-copy r s 0 n "
        "   str-fill r div16 n div16 b "
        "   r ] "
        " append-copy: func [r s i n] [ "
        "   either < i n [ str-fill r div16 i str-byte s div16 i append-copy r s + i 1 n ] [ none ] ] "
        " string=?: func [a b] [ "
        "   either = length? a length? b [ scmp a b 0 length? a ] [ 0 ] ] "
        " scmp: func [a b i n] [ "
        "   either < i n [ either = string-byte a i string-byte b i [ scmp a b + i 1 n ] [ 0 ] ] [ 1 ] ] "
        " copy: func [s] [ s ] ",
        (long)alloc_addr, (long)alloc_addr);
    return buf;
}

#endif /* R0_S1_M3C_LIB_H */
