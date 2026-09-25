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
    T_USER = 11, T_STRING = 12,
    T_BOUND = 13,  /* FIB-OPT-P4: pre-resolved lexical (depth, slot) reference */
    T_ERROR = 14   /* SIN!: a first-class error value (inert data until RAISEd) */
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
#define mk_string(p)  ((cell)((p) + T_STRING))
#define word_id(v)    ((cell)((v) / 16))
#define int_val(v)    ((cell)((v) / 16))

/* FIB-OPT-P4: a pre-resolved lexical reference encodes (depth, slot) in the
 * payload. depth = CTX_PARENT hops from the current context; slot = binding
 * index (value cell at p + 4 + 2*slot). The word symbol stays in the context
 * for dynamic lookup and introspection. */
#define mk_bound(depth, slot) ((cell)((((depth) * 16 + (slot)) * 16) + T_BOUND))
#define bound_payload(v)      ((cell)((v) / 16))
#define bound_slot(v)         ((cell)(bound_payload(v) % 16))
#define bound_depth(v)        ((cell)(bound_payload(v) / 16))

/* --- native ids (== HOST ids for arithmetic/comparison; specials >100) --- */
enum {
    RN_ADD = 0, RN_SUB = 1, RN_MUL = 2, RN_DIV = 3,
    RN_EQ = 6, RN_LT = 8, RN_GT = 9, RN_LE = 10, RN_GE = 11,
    RN_PRINT = 14,
    RN_VALUES = 100,
    RN_EITHER = 101,
    RN_DO = 102,
    RN_INVOKE = 103,
    RN_REDUCE = 104,
    /* SIN!: construction, inspection, propagation, judge boundary */
    RN_MAKE_ERROR = 105,
    RN_ERRORP = 106,
    RN_ERROR_TYPE = 107,
    RN_ERROR_ID = 108,
    RN_ERROR_ARG = 109,
    RN_RAISE = 110,
    RN_TRAP = 111
};

/* --- memory layout ------------------------------------------------------ */
#define R0S1_HEAP_BASE 40000L
/* The loader heap grows upward to M1_ARENA_BASE (m1_layout.h). The G1E demo
 * launcher (launcher + shop + guide + merchant-flow) plus the per-dispatch
 * re-parse overhead from the G1A route/event bridge outgrew the original
 * 7000-cell arena [40000, 47000). The M1 arena was shrunk from 5 to 4 tasks
 * (tests use <=3) and its base moved up to 50600, giving the loader heap
 * [40000, 50600) = 10600 cells. The merchant-flow demo (real M1 multitasking
 * + dataflow visualisation) outgrew that, so the arena was shrunk again from 4
 * to 3 tasks (the tests and the demo use exactly 3) and its base moved up to
 * 54200, giving the loader heap [40000, 54200) = 14200 cells. The tuple-space
 * /dataflow demo + its canvas visualisation outgrew that, so the per-task RS
 * was trimmed (3200 -> 3000) and the arena base moved up to 54800, giving the
 * loader heap [40000, 54800) = 14800 cells. */
#define R0S1_HEAP_LIMIT 54800L
#define R0S1_CTX_CAP   16      /* max bindings per child context */

/* FIB-OPT-P8: contexts with fewer than this many bindings are resolved by the
 * ordered linear scan and do NOT build/maintain the P5 hash index. The hash is
 * built lazily when a context's binding count reaches HASH_MIN. */
#define HASH_MIN       4

/* reserved symbol id: "func" is interned first, so mk_word(0) == the `func`
 * keyword. "return" is interned second, so mk_word(1) == the `return`
 * keyword. "raw" (the legacy MASM spelling) is interned third, so
 * mk_word(2) == the `raw` keyword. */
#define FUNC_SYM 0
#define RETURN_SYM 1
#define RAW_SYM 2

/* runtime variable cells (in M) */
/* Relocated up (M3, then M3C): the emitted collector/evaluator grew past the
 * old RV boundary. M3 moved the runtime state to ~8567; M3C moved it again to
 * [24576, ~25315) (the free gap between the return-stack top RS_INIT=24576 and
 * the managed heap GC_HEAP_BASE=32768) so the code region [256, 24576) is now
 * clear. All RV_* are RV_BASE-relative. */
enum {
    RV_BASE = 24595,
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
    /* SIN!: the error being propagated (set by RAISE or a runtime check,
     * consumed by the judge landing pad; never live across an allocation
     * except via the data stack). RV_ERRV holds a raised SIN! value, or NONE
     * when the landing pad must build one from RV_ERRT/RV_ERRI/RV_ERRA. */
    RV_ERRT     = RV_BASE + 28, /* pending error type (word) */
    RV_ERRI     = RV_BASE + 29, /* pending error id (word or none) */
    RV_ERRA     = RV_BASE + 30, /* pending error argument (any value) */
    RV_ERRV     = RV_BASE + 31, /* pending SIN! value, or NONE */
    RV_ERRRET   = RV_BASE + 32, /* judge's caller return address (raw, transient) */
    RV_ERRUNC   = RV_BASE + 33, /* 1 iff the last run halted on an uncaught error */
    RV_HALTWHY  = RV_BASE + 34, /* why the last run fail-stopped (R0S1_HALT_*), or 0 */
    RV_RES_BUF  = RV_BASE + 40  /* scratch: preserved result set (16 cells) */
};
_Static_assert(RV_ERRUNC < RV_RES_BUF, "SIN! registers must stay below RV_RES_BUF");
_Static_assert(RV_HALTWHY < RV_RES_BUF, "RV_HALTWHY must stay below RV_RES_BUF");

/* Structured reasons for a machine-level fail-stop (not SIN!s: no judge can
 * catch them). A halt with no recorded reason reads as R0S1_HALT_NONE. */
enum {
    R0S1_HALT_NONE = 0,
    R0S1_HALT_CONTEXT_FULL = 1   /* a new binding would exceed the context's capacity */
};

/* Generic scratch cells available to RAW fragments. They live in the free
 * region above the RV cells and (since M3C) above the return-stack top
 * (RS_INIT = 24576), so they never collide with evaluator state, code, or
 * stacks. RAW code may use them for temporaries that must survive across
 * register/state writes. */
#define RV_SCRATCH_A 24651
#define RV_SCRATCH_B 24652
#define RV_SCRATCH_E 24653
#define RV_SCRATCH_F 24654

/* G1B/G1C SPA-dialect output buffer. This is TEMPORARY scratch/output storage,
 * not part of the permanent GUI architecture: the view dialect emits rendered
 * HTML bytes into a fixed, otherwise-unused region just above the GC state
 * (GC_A2 = 25314) and below the managed heap (32768). The layout is a loader-
 * style byte-list block so the unchanged G1A renderer can read it:
 *   M[G1_OUT]      = byte count        (set by emit-finish)
 *   M[G1_OUT + 1]  = site (unused)
 *   M[G1_OUT + 2 + i] = byte i as a tagged int (mk_int)
 * G1_OUT is 16-aligned so mk_block(G1_OUT) is a valid T_BLOCK. */
#define G1_OUT      25344
#define G1_OUT_DATA (G1_OUT + 2)
#define G1_OUT_CAP  4096

/* G1 canvas-visualization output: a second byte-output buffer (same layout as
 * G1_OUT) into which a demo emits a generic visual script during render. The
 * browser host reads it after the dispatch and draws/animated the Canvas; the
 * native test reads it to assert the emitted path matches the real execution.
 * Lives in the free gap just below the managed heap (29456..32018). */
#define G1_VIS      29456
#define G1_VIS_DATA (G1_VIS + 2)
#define G1_VIS_CAP  2560

/* M3 datatype-library state (free region above the GC state, below the D1
 * state records). BUILTIN_BASE is the fixed 16-slot BUILTIN_TYPE table; the
 * meta-descriptor payload and the built-in descriptor payloads are at fixed
 * addresses at the bottom of the managed heap (seeded by the M3 harness). */
#define GC_META         24576    /* cell holding the datatype! meta-descriptor  */
#define BUILTIN_BASE    24577    /* BUILTIN_TYPE[0..15]                         */
#define RV_SCRATCH_C    24593    /* datatype-library RAW scratch                */
#define RV_SCRATCH_D    24594    /* datatype-library RAW scratch                */
#define GC_META_PAYLOAD 32784   /* payload of the datatype! meta-descriptor    */
#define BUILTIN0_PAYLOAD 32816  /* payload of the integer! built-in descriptor */
#define BUILTIN_STRING_PAYLOAD 33168 /* payload of the string! built-in descriptor (slot 12) */

/* --- M2 GC: managed heap + collector state (above the frozen S1) -----------
 * A non-moving, stop-the-world, exact mark/sweep collector over the ONE shared
 * GLON heap [GC_HEAP_BASE, GC_HEAP_LIMIT).  The loader heap
 * [R0S1_HEAP_BASE, R0S1_HEAP_LIMIT) is traced but never swept.  See
 * M2-GC-DESIGN.md. */

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
#define GC_KIND_CTX     1
#define GC_KIND_CLOSURE 2
#define GC_KIND_RAW     3
#define GC_KIND_FRAME   4   /* unused: activation frames live on the return stack (FIB-OPT-P2) */
#define GC_KIND_USER    5
#define GC_KIND_STRING  6
#define GC_KIND_BLOCK   7

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
#define GC_BASE         25034
#define GC_GLOBAL_CTX   25034    /* mirror of global context */
#define GC_LOADER_HP    25035    /* loader-heap bump (shared lalloc) */
#define GC_LIVE_CELLS   25036
#define GC_LIVE_OBJS    25037
#define GC_FREE_BLOCKS  25038
#define GC_FREE_CELLS   25039
#define GC_COLLECT_CNT  25040
#define GC_LAST_RECLAM  25041
#define GC_T1           25042
#define GC_T2           25043
#define GC_T3           25044
#define GC_T4           25045
#define GC_T5           25046
#define GC_T6           25047
#define GC_T7           25048
#define GC_T8           25049
#define GC_C1           25050
#define GC_C2           25051
#define GC_C3           25052
#define GC_C4           25053
#define GC_C5           25054
#define GC_C6           25055
#define GC_WL_SP        25056
#define GC_WORKLIST     25057    /* 256 entries -> ..25312 */
#define GC_A1           25313    /* alloc()'s persistent extent */
#define GC_A2           25314    /* alloc()'s persistent kind */

/* --- FIB-PROFILE-P1: profiler counters (free region 24655..24858) -----------
 * Diagnostic-only counters, reset per r0_s1_run and read by the profiler
 * driver. They are NEVER read or written by GLON semantics; they exist purely
 * for instrumentation. The emitted increments are compiled only under
 * -DR0_S1_PROFILE, so a baseline build has zero added overhead (the cells are
 * simply never written). They sit in the free gap above the RV scratch cells
 * and below the D1 buffers (DBGEE_BUF=24859). */
#define PF_BASE         24655
#define PF_CLOSURE      24655   /* closure invocations (r_invoke_closure)       */
#define PF_SUBEXPR      24656   /* sub-expression dispatches (r_subexpr)         */
#define PF_BLKEVAL      24657   /* block-evaluator loop iterations (r_block_eval) */
#define PF_LOOKUP       24658   /* logical word lookups (r_lookup)               */
#define PF_LK_SLOTS     24659   /* binding slots examined (lookup inner loop)    */
#define PF_LK_PARENT    24660   /* parent-context hops (lookup outer loop)       */
#define PF_NATIVE       24661   /* native invocations total                      */
#define PF_NAT_LE       24662   /* <=                                            */
#define PF_NAT_SUB      24663   /* -                                             */
#define PF_NAT_ADD      24664   /* +                                             */
#define PF_NAT_EITHER   24665   /* either                                        */
#define PF_NAT_OTHER    24666   /* any other native                              */
#define PF_ALLOCS       24667   /* r_alloc calls                                 */
#define PF_ALLOC_CELLS  24668   /* cells consumed (extent sum)                   */
#define PF_ALLOC_CTX    24669   /* context allocations                           */
#define PF_ALLOC_FRAME  24670   /* frame allocations                             */
#define PF_RAW          24671   /* RAW invocations                               */
#define PF_TRACE        24672   /* runtime trace-enable flag (0/1)               */
#define PF_DEPTH        24673   /* current closure nesting depth (trace/depth)   */
#define PF_MAXDEPTH     24674   /* max closure nesting depth seen                */
#define PF_PROMOTE      24675   /* stack->managed context promotions             */
#define PF_LEX_DIRECT   24676   /* lexical references resolved directly by slot  */
#define PF_LEX_LOCAL    24677   /* direct depth-0 lexical accesses               */
#define PF_LEX_PARENT   24678   /* direct parent lexical accesses (depth > 0)    */
#define PF_HASH_PROBES  24679   /* P5 hash-index probes (per bucket examined)    */
#define PF_HASH_HITS    24680   /* P5 hash-index hits                            */
#define PF_HASH_MISSES  24681   /* P5 hash-index misses (empty bucket)           */
#define PF_HASH_COLLISIONS 24682/* P5 collision probes (probe past the first)    */
#define PF_HASH_FALLBACK 24683  /* P5 fallback linear scans (index full/overflow) */
#define PF_HASH_FALLBACK_SLOTS 24684 /* P5 slots examined by fallback linear scan  */

/* context layout: [parent, count, cap, (word,value)*cap, hash[cap]].
 * FIB-OPT-P5: the hash index maps word_id -> slot. hash[i] = word_id*256 + slot
 * (occupied) or -1 (empty); h(word_id) = word_id % cap. The index is a fast
 * path only; the ordered (word,value) pairs remain authoritative. */
enum { CTX_PARENT = 0, CTX_COUNT = 1, CTX_CAP = 2, CTX_DATA = 3 };
#define CTX_HASH      (CTX_DATA + 2 * R0S1_CTX_CAP)  /* hash index offset (35) */
#define R0S1_CTX_CELLS 64    /* 16-aligned stack-context size (3+32+16=51 -> 64) */
#define HASH_EMPTY    (-1)   /* empty hash bucket sentinel */

/* closure layout: [spec, body, captured-context, func-site-id, depth-bias]
 * CLOSURE_BIAS is the "Guard of Binding" depth offset: 0 for a hand-written
 * FUNC body (its T_BOUND depths already account for the closure's own context),
 * +1 for a body block that was manufactured through a runtime factory and whose
 * originating activation was live in the lexical ancestry (its depths are
 * relative to that enclosing func, one level outside the closure). */
enum { CLOSURE_SPEC = 0, CLOSURE_BODY = 1, CLOSURE_CTX = 2, CLOSURE_SITE = 3, CLOSURE_BIAS = 4 };

/* RAW callable layout: [entry-address, arity] */
enum { RAW_ENTRY = 0, RAW_ARITY = 1 };

/* block layout: [count, return-site-id, elem0, elem1, ...] */
enum { BLK_COUNT = 0, BLK_SITE = 1, BLK_DATA = 2 };

/* Escape-time binding law: an activation-dependent block (one whose executable
 * tree carries a T_BOUND or RETURN that would resolve against its originating
 * activation) is marked by storing its BLK_SITE NEGATED, so `raw_site < 0`
 * tests the flag with only the frozen arithmetic host ops (there is no AND).
 * Decoding recovers the original non-negative site id. */
#define BLK_SITE_DEP(site)      (-((cell)(site)) - 1)
#define BLK_SITE_ISDEP(raw)     ((raw) < 0)
#define BLK_SITE_DECODE(raw)    (((raw) < 0) ? (-(raw) - 1) : (raw))

/* Owning func-site of a cap-16 runtime context (every stack context and every
 * promoted managed context has cap R0S1_CTX_CAP). It lives in the unused tail
 * of the fixed 64-cell context (3 + 32 + 16 = 51 cells are used), so the
 * escape-law checks need no per-context size increase. The global context is a
 * cap-256 loader context and is special-cased (owner site 0). */
#define CTX_ESCSITE (CTX_DATA + 3 * R0S1_CTX_CAP)   /* 51 */

/* Static site-parent metadata for the escape law. M[SITE_PARENT_BASE + sid] is
 * the lexical enclosing func-site of site sid (site 0 is the root). It lives in
 * the free gap above the G1 visual buffer and below the managed heap
 * ([32018,32768)), holds raw site ids only, and is never GC-scanned. */
#define SITE_PARENT_BASE 32024
#define SITE_PARENT_CAP  640

/* Private scratch cells for the escape-law enforcement helpers. They live in
 * the authoritative free gap after the site-parent table and before the managed
 * heap: [SITE_PARENT_BASE+SITE_PARENT_CAP, GC_HEAP_BASE). This depends only on
 * the two authoritative bounds below, not on the (unmacro'd) end of the D1 /
 * runtime state region. An escape check therefore never touches SCRATCH_A..F,
 * the GC work list/state, M1 task state, D1 state or any blessed RAW cell. */
#define RV_ESC_A 32664
#define RV_ESC_B 32665
#define RV_ESC_C 32666
#define RV_ESC_E 32667
#define RV_ESC_F 32668
#define RV_ESC_N 32669
#define RV_ESC_P 32670
#define RV_ESC_W 32671

/* Compile-time layout guards (standard C11/_Static_assert; no bespoke layout
 * mechanism). These fail the build if a future layout edit makes either fixed
 * region overlap its neighbours. The site-parent table must sit above the G1
 * visual buffer and below the managed heap; the escape scratch must sit above
 * the site-parent table and below the managed heap. */
_Static_assert(SITE_PARENT_BASE >= G1_VIS + 2 + G1_VIS_CAP,
               "SITE_PARENT table overlaps the G1 visual buffer");
_Static_assert(SITE_PARENT_BASE + SITE_PARENT_CAP <= GC_HEAP_BASE,
               "SITE_PARENT table overlaps the managed heap");
_Static_assert(RV_ESC_A >= SITE_PARENT_BASE + SITE_PARENT_CAP,
               "RV_ESC_* overlaps the site-parent table");
_Static_assert(RV_ESC_W < GC_HEAP_BASE,
               "RV_ESC_* overlaps the managed heap");

/* activation frame (linked list in M): [prev, site, SP, RP, IP, CTX, CUR, END, BLK, bias] */
enum {
    FRAME_PREV = 0, FRAME_SITE = 1, FRAME_SP = 2, FRAME_RP = 3, FRAME_IP = 4,
    FRAME_CTX = 5, FRAME_CUR = 6, FRAME_END = 7, FRAME_BLK = 8, FRAME_BIAS = 9,
    /* Judge frames only (built by JUDGE, always 16 cells): the judge's caller
     * return address. A judge frame is recognised by FRAME_IP == the judge
     * landing pad, an address no closure invocation ever returns to. */
    FRAME_TRAPRET = 10
};

/* SIN! payload (a managed GC_KIND_USER object, so the collector traces it
 * generically): [desc = none, count = 3, type, id, arg]. */
enum { ERR_DESC = 0, ERR_COUNT = 1, ERR_TYPE = 2, ERR_ID = 3, ERR_ARG = 4, ERR_NFIELDS = 3 };

/* --- loader + runtime API ------------------------------------------------ */

/* reset loader state (heap, interner) and build the evaluator code + global
 * context. Returns the EVAL_LOOP entry point. */
cell r0_s1_init(void);

/* parse R0 source into a block (loader). *err = 0 on success. */
cell r0_s1_parse(const char *src, int *err);
/* Why the last r0_s1_parse failed. A failed parse is rolled back completely
 * (loader heap, func-site counter, symbol table) and returns R0_NONE. These are
 * load-time failures, before any Glon runs, so they are never SIN!s. */
enum {
    R0S1_PARSE_OK = 0,
    R0S1_PARSE_SYNTAX = 1,             /* malformed source (e.g. an unclosed [) */
    R0S1_PARSE_SYMBOL_TABLE_FULL = 2,  /* too many distinct words in the session */
    R0S1_PARSE_LOADER_EXHAUSTED = 3,   /* the loader heap has no room for this source */
    R0S1_PARSE_SITE_TABLE_FULL = 4,    /* too many func literals in the session */
    R0S1_PARSE_TOO_LARGE = 5           /* a block/string exceeds the 512-element limit */
};
int  r0_s1_parse_error_kind(void);

/* run a block: set runtime cells, s1_run(EVAL_LOOP), inspect results.
 * Returns result arity N (>= 0), or -1 on error. */
int r0_s1_run(cell block);

/* Persistent-machine variant (G1).  Like r0_s1_run but resets only transient
 * execution state (SP/RP/IP and the runtime scratch cells) and PRESERVES the
 * managed-heap frontier (REG_HP), so managed STRING!/closure values allocated
 * by the loaded program (or an earlier interaction) survive across route/event
 * calls instead of being overwritten by s1_reset's heap re-base. */
int r0_s1_run_persistent(cell block);

/* FIB-OPT-P10A: run a block exactly like r0_s1_run, but invoke the compiled
 * S1 executor `run_fn` (signature void fn(cell *M, cell start)) instead of the
 * interpreted s1_run(). Used only by the compiled-S1 benchmark driver. */
int r0_s1_run_compiled(cell block, void (*run_fn)(cell *, cell));

/* i-th result (0-based) after r0_s1_run returned N */
cell r0_s1_result(int i, int N);

/* instrumentation */
cell r0_s1_ip_start(void), r0_s1_ip_end(void);
int  r0_s1_ran_cleanly(void);   /* 1 iff the last run reached the normal halt */
/* 1 iff the last run halted because a raised SIN! reached no judge; fills the
 * error's type/id/arg (any pointer may be NULL). */
int  r0_s1_uncaught_error(cell *type, cell *id, cell *arg);
/* the spelling of interned symbol `id` (a word's word_id), or NULL */
const char *r0_s1_sym_name(cell id);
/* r0_s1_show.c: run `src` (len bytes, without the outer [ ]) persistently and
 * write its outcome as text into out (NUL-terminated); returns the length.
 * Shared by the primer doc-tests and the WASM host's glon_run. */
int  r0_s1_show_run(const char *src, unsigned int len, char *out, int cap);
/* r0_s1_show.c: mold one value as text (the same rendering as show_run);
 * returns the length written (NUL-terminated). */
int  r0_s1_mold(cell v, char *out, int cap);

/* Global-context usage: current binding count and capacity. */
void r0_s1_global_usage(cell *count, cell *cap);

/* r0_s1_session.c: run one cell (source WITHOUT the outer [ ]) in the persistent
 * session and classify the outcome from structured runtime state only. */
enum {
    R0S1_OUT_OK = 0,              /* ran cleanly; `count` results, read with r0_s1_result(i, count) */
    R0S1_OUT_PARSE_ERROR = 1,     /* the cell is not valid source (detail: syntax / too_large) */
    R0S1_OUT_RESOURCE_ERROR = 2,  /* a fixed session capacity is exhausted (detail says which) */
    R0S1_OUT_UNCAUGHT_SIN = 3,    /* a raised SIN! reached no judge (sin_type/id/arg) */
    R0S1_OUT_HALT = 4             /* any other machine-level fail-stop (detail: stack_sentry / machine) */
};
enum {
    R0S1_DETAIL_NONE = 0,
    R0S1_DETAIL_SYNTAX = 1,
    R0S1_DETAIL_TOO_LARGE = 2,
    R0S1_DETAIL_SYMBOL_TABLE_FULL = 3,
    R0S1_DETAIL_LOADER_EXHAUSTED = 4,
    R0S1_DETAIL_SITE_TABLE_FULL = 5,
    R0S1_DETAIL_CONTEXT_FULL = 6,
    R0S1_DETAIL_STACK_SENTRY = 7,
    R0S1_DETAIL_MACHINE = 8
};
typedef struct {
    int status, detail;
    int count;                    /* OK: number of results left by the cell */
    cell sin_type, sin_id, sin_arg;
} r0_s1_outcome;
int  r0_s1_session_run(const char *src, unsigned int len, r0_s1_outcome *out);
/* Run an already-parsed program in the persistent session (same classification
 * as r0_s1_session_run). Used by hosts that parse a whole source file with
 * r0_s1_parse, e.g. the native `glon` CLI. */
int  r0_s1_session_run_block(cell prog, r0_s1_outcome *out);
/* Classify the parse failure recorded by r0_s1_parse_error_kind(). */
void r0_s1_session_parse_error(r0_s1_outcome *out);
const char *r0_s1_outcome_status_name(int status);
const char *r0_s1_outcome_detail_name(int detail);
int  r0_s1_stack_sentry_fired(void);  /* 1 iff the last run violated SP/RP bounds */
/* R0S1_HALT_* reason the last run fail-stopped (R0S1_HALT_NONE after a clean
 * run, an uncaught SIN!, or a halt with no recorded reason). */
int  r0_s1_halt_reason(void);
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

/* FIB-PROFILE-P1: profiler statistics (read the PF_* counter cells). Only
 * meaningful in a -DR0_S1_PROFILE build; reads 0 otherwise. */
typedef struct {
    long closure, subexpr, blkeval, lookup, lk_slots, lk_parent;
    long native, nat_le, nat_sub, nat_add, nat_either, nat_other;
    long allocs, alloc_cells, alloc_ctx, alloc_frame, raw;
    long max_depth, promote;
    long lex_direct, lex_local, lex_parent;
    long hash_probes, hash_hits, hash_misses, hash_collisions, hash_fallback, hash_fallback_slots;
} r0_s1_pf_stats;
void r0_s1_pf_read(r0_s1_pf_stats *out);

#endif /* R0_S1_H */
