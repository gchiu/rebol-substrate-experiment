/* r0.h - a very small REBOL-like language (R0) built above the frozen S1
 * substrate. See R0-ARCHITECTURE.md for the normative specification.
 *
 * R0 stores its tagged values and heap objects in S1's flat cell memory M
 * (the S1 substrate's memory), using low-4-bit tags. The evaluator is host
 * (C) code that walks those objects; it uses a host-side data stack and
 * control stack (arrays of S1 cells), and realizes non-local control transfer
 * by frame-by-frame unwind to a recorded SP baseline. The RAW trapdoor runs
 * genuine S1 machine code (built with the S1 assembler) via s1_run.
 *
 * S1 is FROZEN: nothing here modifies s1.c / s1.h / tests.c / adversarial.c /
 * claims.c, and no S1 primitive is added or reinterpreted.
 */
#ifndef R0_H
#define R0_H

#include "s1.h"
#include <stddef.h>

/* S1's memory is the R0 heap: cells with low-4-bit tags. */
extern cell M[MEM_CELLS];

/* --- tags (low 4 bits) ------------------------------------------------ */
enum {
    TAG_INT = 0, TAG_NONE = 1, TAG_WORD = 2, TAG_SET = 3, TAG_GET = 4,
    TAG_LIT = 5, TAG_BLOCK = 6, TAG_CONTEXT = 7, TAG_CLOSURE = 8,
    TAG_NATIVE = 9, TAG_RAW = 10
};

#define R0_NONE        ((cell)0x1)
#define r0_tag(v)      ((int)((v) & 15))
#define r0_untag(v)    ((cell)((v) - ((v) & 15)))
#define mk_int(n)      ((cell)((n) * 16))
#define mk_word(id)    ((cell)((id) * 16 + TAG_WORD))
#define mk_set(id)     ((cell)((id) * 16 + TAG_SET))
#define mk_get(id)     ((cell)((id) * 16 + TAG_GET))
#define mk_lit(id)     ((cell)((id) * 16 + TAG_LIT))
#define mk_block(p)    ((cell)((p) + TAG_BLOCK))
#define mk_context(p)  ((cell)((p) + TAG_CONTEXT))
#define mk_closure(p)  ((cell)((p) + TAG_CLOSURE))
#define mk_native(id)  ((cell)((id) * 16 + TAG_NATIVE))
#define mk_raw(p)      ((cell)((p) + TAG_RAW))
#define word_id(v)     ((cell)((v) / 16))
#define native_id(v)   ((cell)((v) / 16))
#define int_val(v)     ((cell)((v) / 16))

/* --- native ids -------------------------------------------------------- */
enum {
    N_ADD = 0, N_SUB, N_MUL, N_DIV,
    N_EQ, N_NE, N_LT, N_GT, N_LE, N_GE,
    N_PRINT, N_IF, N_LOOP, N_DO, N_VALUES, N_BREAK, N_THROW,
    N_CATCH, N_LEN, N_PICK,
    N_MAKE_GEN, N_NEXT,
    N_NATIVES
};

/* --- heap / interner / caps ------------------------------------------- */
#define R0_HEAP_BASE  40000L
#define R0_HEAP_LIMIT 47000L
#define R0_DS_SIZE    4096
#define R0_CS_SIZE    256
#define R0_MAX_ARITY  16
#define R0_MAX_SYMS   256
#define R0_MAX_RSITES 512
#define R0_MAX_BLOCK  256
#define R0_MAX_BINDINGS 128

/* --- closures: fixed layout [spec, body, ctx, site_id] ---------------- */
#define CLOSURE_SPEC 0
#define CLOSURE_BODY 1
#define CLOSURE_CTX  2
#define CLOSURE_SITE 3
#define CLOSURE_SIZE 4

/* --- context layout: [parent, count, cap, (word,value)...] ------------- */
#define CTX_PARENT 0
#define CTX_COUNT  1
#define CTX_CAP    2
#define CTX_DATA   3

/* --- transfer status --------------------------------------------------- */
enum { ST_NORMAL = 0, ST_BREAK, ST_RETURN, ST_THROW };

/* --- control frame ----------------------------------------------------- */
typedef struct {
    int kind;       /* 0=loop, 1=function, 2=catch */
    int id;         /* func-site id (kind==1) */
    int saved_sp;   /* data-stack baseline */
} r0_frame;

/* a pending non-local transfer (RETURN / THROW carry a result set) */
typedef struct {
    int status;            /* ST_NORMAL..ST_THROW */
    int target_site;       /* for ST_RETURN */
    cell vals[R0_MAX_ARITY];
    int nvals;
} r0_transfer;

/* --- public API -------------------------------------------------------- */

/* reset all R0 runtime state (heap, stacks, interner, tables); build the
 * global context with all bound natives. Returns the global CONTEXT value. */
cell r0_init(void);

/* parse R0 source text into a top-level BLOCK value (returns mk_block or
 * NONE on error). Records func-site ids and return-site entries. */
cell r0_parse(const char *src, int *errline);

/* evaluate a block (code position) in context ctx; result set is left on the
 * R0 data stack. Returns the arity N; the N results are accessible via
 * r0_result(i) in order. Returns -1 on an uncaught R0 error. */
int r0_eval(cell block, cell ctx);

/* result access after r0_eval returns arity N */
cell r0_result(int i);

/* --- introspection for tests ------------------------------------------ */
int   r0_ds_depth(void);      /* current data-stack depth (sp) */
int   r0_ds_max(void);        /* max observed data-stack depth */
int   r0_cs_depth(void);      /* current control-stack depth */
int   r0_cs_max(void);        /* max observed control-stack depth */
cell  r0_global_context(void);
cell  r0_intern(const char *name);   /* returns a WORD value */
int   r0_symbol_count(void);
long  r0_heap_used(void);

#endif /* R0_H */
