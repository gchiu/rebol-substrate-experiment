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
    T_BLOCK = 6, T_CONTEXT = 7, T_CLOSURE = 8, T_NATIVE = 9, T_RAW = 10,
    T_USER = 11
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
#define mk_user(p)    ((cell)((p) + T_USER))
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
 * keyword. "raw" is interned third, so mk_word(2) == the `raw` keyword. */
#define FUNC_SYM 0
#define RETURN_SYM 1
#define RAW_SYM 2

/* runtime variable cells (in M) */
/* Relocated up (M3): the emitted collector/evaluator grew past the old RV
 * boundary once the generic user-object trace and the datatype library RAW
 * fragments were added. M1 (9001..9024) and the GC state (9025..9305) moved up
 * with it, so the code region [256, 8567) is now clear. All RV_* are
 * RV_BASE-relative. */
enum {
    RV_BASE = 8586,
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

/* Generic scratch cells available to RAW fragments. They live in the free
 * region above the RV cells and below the data stack (DS_INIT = 16384), so
 * they never collide with evaluator state, code, or stack. RAW code may use
 * them for temporaries that must survive across register/state writes. */
#define RV_SCRATCH_A 8642
#define RV_SCRATCH_B 8643
#define RV_SCRATCH_E 8644
#define RV_SCRATCH_F 8645

/* M3 datatype-library state (free region above the GC state, below the D1
 * state records). BUILTIN_BASE is the fixed 16-slot BUILTIN_TYPE table; the
 * meta-descriptor payload and the built-in descriptor payloads are at fixed
 * addresses at the bottom of the managed heap (seeded by the M3 harness). */
#define GC_META         8567    /* cell holding the datatype! meta-descriptor  */
#define BUILTIN_BASE    8568    /* BUILTIN_TYPE[0..15]                         */
#define RV_SCRATCH_C    8584    /* datatype-library RAW scratch                */
#define RV_SCRATCH_D    8585    /* datatype-library RAW scratch                */
#define GC_META_PAYLOAD 32784   /* payload of the datatype! meta-descriptor    */
#define BUILTIN0_PAYLOAD 32816  /* payload of the integer! built-in descriptor */

/* --- M2 GC: managed heap + collector state (above the frozen S1) -----------
 * A non-moving, stop-the-world, exact mark/sweep collector over the ONE shared
 * GLON heap [GC_HEAP_BASE, GC_HEAP_LIMIT).  The loader heap (40000..47000) is
 * traced but never swept.  See M2-GC-DESIGN.md. */

#define GC_HEAP_BASE   32768L   /* == s1.c HEAP_BASE (managed runtime heap) */
#define GC_HEAP_LIMIT  40000L   /* == R0S1_HEAP_BASE (loader heap start)   */

/* standard S1 stack tops (also used to locate the scheduler/main world) */
#define R0S1_DS_INIT   16384L
#define R0S1_RS_INIT   24576L

/* allocation header: 16-cell stride before each 16-aligned payload. */
#define GC_HDR_SIZE     0
#define GC_HDR_FLAGS    1
#define GC_HDR_STRIDE   16
#define GC_FLAG_ALLOC   1
#define GC_FLAG_MARK    2
#define GC_KIND_BLOCK   0
#define GC_KIND_CTX     1
#define GC_KIND_CLOSURE 2
#define GC_KIND_RAW     3
#define GC_KIND_FRAME   4
#define GC_KIND_USER    5

/* collector state cells (fixed, disjoint from every other region).  These are
 * plain integer literals (not expressions) so both the C emitter and the RAW
 * assembler (which stringifies them via XSTR) can use them.
 *
 * Scratch discipline (the collector calls itself recursively, so nested calls
 * must not clobber the caller's live cells):
 *   GC_T1..GC_T5  mark/trace leaf functions (mark_value, mark_push, trace_*,
 *                 mark_frame, mark_closure_inline)
 *   GC_T6..GC_T8  scan functions + drain + sweep (scan_values, scan_closures)
 *   GC_C1..GC_C6  collect()'s own persistent state (SP0, RP0, task index,
 *                 record, arena base, prev_free) */
#define GC_BASE         9025
#define GC_GLOBAL_CTX   9025    /* mirror of global context */
#define GC_LOADER_HP    9026    /* loader-heap bump (shared lalloc) */
#define GC_LIVE_CELLS   9027
#define GC_LIVE_OBJS    9028
#define GC_FREE_BLOCKS  9029
#define GC_FREE_CELLS   9030
#define GC_COLLECT_CNT  9031
#define GC_LAST_RECLAM  9032
#define GC_T1           9033
#define GC_T2           9034
#define GC_T3           9035
#define GC_T4           9036
#define GC_T5           9037
#define GC_T6           9038
#define GC_T7           9039
#define GC_T8           9040
#define GC_C1           9041
#define GC_C2           9042
#define GC_C3           9043
#define GC_C4           9044
#define GC_C5           9045
#define GC_C6           9046
#define GC_WL_SP        9047
#define GC_WORKLIST     9048    /* 256 entries -> ..9303 */
#define GC_A1           9304    /* alloc()'s persistent extent */
#define GC_A2           9305    /* alloc()'s persistent kind */

/* context layout: [parent, count, cap, (word,value)...] */
enum { CTX_PARENT = 0, CTX_COUNT = 1, CTX_CAP = 2, CTX_DATA = 3 };

/* closure layout: [spec, body, captured-context, func-site-id] */
enum { CLOSURE_SPEC = 0, CLOSURE_BODY = 1, CLOSURE_CTX = 2, CLOSURE_SITE = 3 };

/* RAW callable layout: [entry-address, arity] */
enum { RAW_ENTRY = 0, RAW_ARITY = 1 };

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

/* M2 GC diagnostics (test/audit only; not GLON language features) */
cell r0_s1_gc_collect_addr(void);  /* S1 address of the collector entry */
cell r0_s1_alloc_addr(void);       /* M3: S1 address of the managed allocator  */
cell r0_s1_lookup_addr(void);      /* M3: S1 address of the context-lookup      */
void r0_s1_seed_datatypes(void);   /* M3: bind built-in type words + seed heap */
long r0_s1_gc_count(void);         /* collection count */
long r0_s1_gc_live_cells(void);    /* live cells after last collection */
long r0_s1_gc_live_objs(void);     /* live objects after last collection */
long r0_s1_gc_free_cells(void);    /* free (reusable) cells after last collection */
long r0_s1_gc_free_blocks(void);   /* free block count after last collection */
long r0_s1_gc_reclaimed(void);     /* cells reclaimed by last collection */
long r0_s1_heap_high(void);        /* REG_HP (heap frontier / high water) */

#endif /* R0_S1_H */
