/* r0_s1.h - R0-on-S1: a minimal R0 evaluator running as S1 machine code.
 *
 * C is only the toolchain (parse/load/assemble/run/inspect). All evaluation
 * semantics are S1 code assembled by r0_s1_runtime.c. See R0-S1-PHASE1.md.
 *
 * S1 is FROZEN: nothing here modifies s1.c/s1.h/tests.c/adversarial.c/claims.c
 * and no primitive is added or reinterpreted.
 */
#ifndef R0_S1_H
#define R0_S1_H

#include "s1.h"

/* S1's flat memory (defined in s1.c) */
extern cell M[MEM_CELLS];

/* --- R0 tags (low 4 bits; same scheme as the architecture) ------------- */
enum {
    T_INT = 0, T_NONE = 1, T_WORD = 2, T_SET = 3, T_GET = 4, T_LIT = 5,
    T_BLOCK = 6, T_CONTEXT = 7, T_CLOSURE = 8, T_NATIVE = 9, T_RAW = 10
};

#define R0_NONE       ((cell)0x1)
#define r0_tag(v)     ((int)((v) & 15))
#define r0_untag(v)   ((cell)((v) - ((v) & 15)))
#define mk_int(n)     ((cell)((n) * 16))
#define mk_word(id)   ((cell)((id) * 16 + T_WORD))
#define mk_set(id)    ((cell)((id) * 16 + T_SET))
#define mk_get(id)    ((cell)((id) * 16 + T_GET))
#define mk_lit(id)    ((cell)((id) * 16 + T_LIT))
#define mk_block(p)   ((cell)((p) + T_BLOCK))
#define mk_context(p) ((cell)((p) + T_CONTEXT))
#define mk_closure(p) ((cell)((p) + T_CLOSURE))
#define mk_native(id) ((cell)((id) * 16 + T_NATIVE))
#define mk_raw(p)     ((cell)((p) + T_RAW))
#define word_id(v)    ((cell)((v) / 16))
#define int_val(v)    ((cell)((v) / 16))

/* --- native ids (== HOST ids for arithmetic/comparison; specials >100) --- */
enum {
    RN_ADD = 0, RN_SUB = 1, RN_MUL = 2, RN_DIV = 3,
    RN_EQ = 6, RN_LT = 8, RN_GT = 9, RN_LE = 10, RN_GE = 11,
    RN_PRINT = 14,
    RN_VALUES = 100,
    RN_EITHER = 101,
    RN_DO = 102
};

/* --- memory layout ------------------------------------------------------ */
#define R0S1_HEAP_BASE 40000L
#define R0S1_HEAP_LIMIT 47000L
#define R0S1_CTX_CAP   16      /* max bindings per child context */

/* reserved symbol id: "func" is interned first, so mk_word(0) == the `func`
 * keyword. "return" is interned second, so mk_word(1) == the `return`
 * keyword. The emitter compares against the literal mk_word(0)/mk_word(1). */
#define FUNC_SYM 0
#define RETURN_SYM 1

/* runtime variable cells (in M) */
enum {
    RV_BASE = 8192,
    RV_CUR  = RV_BASE + 0,   /* current element address */
    RV_END  = RV_BASE + 1,   /* one-past-last element address */
    RV_CTX  = RV_BASE + 2,   /* current context (tagged CONTEXT) */
    RV_WORD = RV_BASE + 3,   /* set-word / param target (persistent) */
    RV_NAT  = RV_BASE + 4,   /* native id (persistent) */
    RV_HOSTCALLS = RV_BASE + 5, /* HOST-call counter */
    RV_N    = RV_BASE + 6,   /* scratch */
    RV_T1   = RV_BASE + 7, RV_T2 = RV_BASE + 8, RV_T3 = RV_BASE + 9,
    RV_T4   = RV_BASE + 10, RV_T5 = RV_BASE + 11, RV_T6 = RV_BASE + 12,
    /* Phase 2: closure application frame (persistent across nested CALLs;
     * saved on RP where noted) */
    RV_CLOSURE  = RV_BASE + 13, /* untagged closure ptr (persistent) */
    RV_ARITY    = RV_BASE + 14, /* arity (persistent) */
    RV_CHILD    = RV_BASE + 15, /* child context (tagged; used in bind loop) */
    RV_BODY     = RV_BASE + 16, /* body block (transient) */
    RV_NVALS    = RV_BASE + 17, /* values: collected-count (persistent) */
    RV_RPMIN    = RV_BASE + 20, /* instrumentation: min RP seen (max depth) */
    RV_SPMIN    = RV_BASE + 21, /* instrumentation: min SP seen (max depth) */
    /* Phase 3A: non-local return */
    RV_BLK      = RV_BASE + 22, /* current block base (raw ptr; saved/restored) */
    RV_FRAME    = RV_BASE + 23, /* current activation frame ptr (0 = none) */
    RV_SITE     = RV_BASE + 24, /* scratch: return target site-id */
    RV_SIP      = RV_BASE + 25, /* scratch: saved return IP */
    RV_SRP      = RV_BASE + 26, /* scratch: saved caller RP */
    RV_FNEW     = RV_BASE + 27, /* scratch: new frame ptr */
    RV_RES_BUF  = RV_BASE + 40  /* scratch: preserved result set (16 cells) */
};

/* context layout: [parent, count, cap, (word,value)...] */
enum { CTX_PARENT = 0, CTX_COUNT = 1, CTX_CAP = 2, CTX_DATA = 3 };

/* closure layout: [spec, body, captured-context, func-site-id] */
enum { CLOSURE_SPEC = 0, CLOSURE_BODY = 1, CLOSURE_CTX = 2, CLOSURE_SITE = 3 };

/* block layout: [count, return-site-id, elem0, elem1, ...] */
enum { BLK_COUNT = 0, BLK_SITE = 1, BLK_DATA = 2 };

/* activation frame (linked list in M): [prev, site, SP, RP, IP, CTX, CUR, END, BLK] */
enum {
    FRAME_PREV = 0, FRAME_SITE = 1, FRAME_SP = 2, FRAME_RP = 3, FRAME_IP = 4,
    FRAME_CTX = 5, FRAME_CUR = 6, FRAME_END = 7, FRAME_BLK = 8
};

/* --- loader + runtime API ------------------------------------------------ */

/* reset loader state (heap, interner) and build the evaluator code + global
 * context. Returns the EVAL_LOOP entry point. */
cell r0_s1_init(void);

/* parse R0 source into a block (loader). *err = 0 on success. */
cell r0_s1_parse(const char *src, int *err);

/* run a block: set runtime cells, s1_run(EVAL_LOOP), inspect results.
 * Returns result arity N (>= 0), or -1 on error. */
int r0_s1_run(cell block);

/* i-th result (0-based) after r0_s1_run returned N */
cell r0_s1_result(int i, int N);

/* instrumentation */
cell r0_s1_ip_start(void), r0_s1_ip_end(void);
cell r0_s1_sp_start(void), r0_s1_sp_end(void);
cell r0_s1_rp_start(void), r0_s1_rp_end(void);
cell r0_s1_rp_min(void);   /* min RP observed (max return depth) */
cell r0_s1_sp_min(void);   /* min SP observed (max data depth) */
cell r0_s1_host_calls(void);
cell r0_s1_code_size(void);   /* cells of emitted S1 code */

#endif /* R0_S1_H */
