/* r0_s1_runtime.c - R0-on-S1: assembler/emitter + bootstrap glue only.
 *
 * This file EMITS the R0 evaluator as S1 machine code. It does not perform
 * R0 evaluation itself. Its C responsibilities are confined to the loader
 * boundary: parse/interning, preloading the global environment into M,
 * assembling S1 code, calling s1_run(), and inspecting results.
 *
 * Phase 2 adds closures, ordinary function application (real S1 CALL/EXIT +
 * >R/R> activations), lexical capture, recursion, and VALUES.
 *
 * S1 is FROZEN. See R0-S1-PHASE1.md.
 */
#include "r0_s1.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ============================= loader state ============================ */

static cell hp;                          /* loader heap bump pointer */
static const char *syms[256];            /* interner: sym_id -> spelling */
static int nsyms;
static cell global_ctx;                  /* tagged CONTEXT value */
static cell main_entry;                  /* top-level S1 entry point */

/* instrumentation */
static cell ip_start, ip_end, sp_start, sp_end, rp_start, rp_end;
static cell code_begin, code_end;

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

static cell intern(const char *name) {
    for (int i = 0; i < nsyms; i++)
        if (strcmp(syms[i], name) == 0) return mk_word(i);
    syms[nsyms] = dup_str(name);
    return mk_word(nsyms++);
}

static cell lalloc(int n) {
    int a = (int)((hp + 15) & ~15L);
    if (a + n > R0S1_HEAP_LIMIT) { fprintf(stderr, "r0_s1: heap exhausted\n"); return -1; }
    hp = a + n;
    return (cell)a;
}

/* ============================= loader: objects ========================= */

static cell make_block(cell cap) {
    cell p = lalloc(2 + (int)cap);   /* [count, site_id, elems...] */
    M[p] = 0;
    M[p + BLK_SITE] = 0;
    return mk_block(p);
}
static cell make_context(cell parent, cell cap) {
    cell p = lalloc(CTX_DATA + 2 * (int)cap);
    M[p + CTX_PARENT] = parent;
    M[p + CTX_COUNT] = 0;
    M[p + CTX_CAP] = cap;
    return mk_context(p);
}
static void bind(cell ctx, cell word, cell value) {
    cell p = r0_untag(ctx);
    cell n = M[p + CTX_COUNT];
    M[p + CTX_DATA + 2 * n] = word;
    M[p + CTX_DATA + 2 * n + 1] = value;
    M[p + CTX_COUNT] = n + 1;
}

/* ============================== parser ================================= */

typedef struct { const char *s; int pos; int err; } parser_t;

/* func-site-id assignment and the lexical enclosing-site stack (parse/load) */
static int site_stack[64];
static int site_depth;
static int next_site;

static void skip_ws(parser_t *P) {
    while (P->s[P->pos] && isspace((unsigned char)P->s[P->pos])) P->pos++;
}

static cell parse_form(parser_t *P);

static cell parse_int(parser_t *P) {
    int neg = 0;
    if (P->s[P->pos] == '-') { neg = 1; P->pos++; }
    long n = 0;
    while (P->s[P->pos] >= '0' && P->s[P->pos] <= '9') { n = n * 10 + (P->s[P->pos] - '0'); P->pos++; }
    return mk_int(neg ? -n : n);
}

static cell parse_word(parser_t *P) {
    int start = P->pos;
    while (P->s[P->pos] && !isspace((unsigned char)P->s[P->pos])
           && P->s[P->pos] != '[' && P->s[P->pos] != ']') P->pos++;
    int len = P->pos - start;
    char buf[64];
    if (len > 63) len = 63;
    memcpy(buf, P->s + start, (size_t)len);
    buf[len] = 0;
    if (len == 4 && strcmp(buf, "none") == 0) return R0_NONE;
    if (buf[len - 1] == ':') { buf[len - 1] = 0; return mk_set((cell)word_id(intern(buf))); }
    if (buf[0] == ':')       { return mk_get((cell)word_id(intern(buf + 1))); }
    if (buf[0] == '\'')      { return mk_lit((cell)word_id(intern(buf + 1))); }
    return intern(buf);
}

static cell parse_form(parser_t *P);
static cell emit_call_fwd(void);   /* forward decl (defined in the emitter section) */

/* ==================== RAW assembler (loader/toolchain) ====================
 * Translates symbolic S1 assembly (mnemonics + operands + labels) into S1
 * cells. This is pure toolchain: it knows the frozen opcodes and a few
 * derived conveniences, and knows NOTHING about R0 control constructs
 * (break/throw or any reified control effect). */

/* Symbolic ABI: user-facing names for memory cells and frame offsets,
 * resolved to numbers at assembly time. This is tooling only — the assembler
 * has no idea what ESCAPE / RETURN / BREAK / THROW / CATCH / UPARSE or any
 * other high-level construct means; it maps generic names to generic cells.
 * The numeric mapping lives in the frozen layout constants above. */
typedef struct { const char *name; cell value; } raw_sym_t;
static const raw_sym_t raw_syms[] = {
    { "REG_IP", REG_IP }, { "REG_SP", REG_SP }, { "REG_RP", REG_RP }, { "REG_HP", REG_HP },
    { "RV_CTX", RV_CTX }, { "RV_CUR", RV_CUR }, { "RV_END", RV_END },
    { "RV_BLK", RV_BLK }, { "RV_FRAME", RV_FRAME },
    { "FRAME_PREV", FRAME_PREV }, { "FRAME_SITE_ID", FRAME_SITE },
    { "FRAME_SAVED_SP", FRAME_SP }, { "FRAME_SAVED_RP", FRAME_RP },
    { "FRAME_SAVED_IP", FRAME_IP }, { "FRAME_SAVED_CTX", FRAME_CTX },
    { "FRAME_SAVED_CUR", FRAME_CUR }, { "FRAME_SAVED_END", FRAME_END },
    { "FRAME_SAVED_BLK", FRAME_BLK },
    { "SCRATCH_A", RV_SCRATCH_A }, { "SCRATCH_B", RV_SCRATCH_B },
};
#define N_RAW_SYMS ((int)(sizeof raw_syms / sizeof raw_syms[0]))

/* resolve an operand token to a cell/offset: an integer literal, or a
 * symbolic ABI name. Returns 0 and sets *ok=0 if unresolved. */
static cell raw_resolve(cell tok, int *ok) {
    if (r0_tag(tok) == T_INT) { *ok = 1; return int_val(tok); }
    if (r0_tag(tok) == T_WORD) {
        const char *nm = syms[word_id(tok)];
        for (int i = 0; i < N_RAW_SYMS; i++)
            if (strcmp(nm, raw_syms[i].name) == 0) { *ok = 1; return raw_syms[i].value; }
    }
    *ok = 0;
    return 0;
}

typedef struct { const char *name; cell addr; } raw_label_t;
static raw_label_t raw_labels[64];
static int raw_nlabels;
typedef struct { cell patch; const char *name; } raw_ref_t;
static raw_ref_t raw_refs[128];
static int raw_nrefs;

static cell raw_find_label(const char *name) {
    for (int i = 0; i < raw_nlabels; i++)
        if (strcmp(raw_labels[i].name, name) == 0) return raw_labels[i].addr;
    return -1;
}

static cell assemble_raw(cell block) {
    cell bp = r0_untag(block);
    int n = (int)M[bp];
    raw_nlabels = 0; raw_nrefs = 0;
    cell entry = asm_here();
    for (int i = 0; i < n; i++) {
        cell tok = M[bp + BLK_DATA + i];
        int t = r0_tag(tok);
        if (t == T_SET) {                 /* label definition */
            raw_labels[raw_nlabels].name = syms[word_id(tok)];
            raw_labels[raw_nlabels].addr = asm_here();
            raw_nlabels++;
            continue;
        }
        if (t != T_WORD) continue;
        const char *nm = syms[word_id(tok)];
        if (strcmp(nm, "LIT") == 0 || strcmp(nm, "INT") == 0 || strcmp(nm, "ARITY") == 0
            || strcmp(nm, "HOST") == 0 || strcmp(nm, "ZBRANCH") == 0
            || strcmp(nm, "BRANCH") == 0 || strcmp(nm, "CALL") == 0) {
            i++;
            cell op = (i < n) ? M[bp + BLK_DATA + i] : R0_NONE;
            if (strcmp(nm, "LIT") == 0 || strcmp(nm, "INT") == 0
                || strcmp(nm, "ARITY") == 0 || strcmp(nm, "HOST") == 0) {
                int ok; cell v = raw_resolve(op, &ok);
                if (!ok) {
                    fprintf(stderr, "r0_s1: unresolved RAW operand in '%s'\n", nm);
                    continue;
                }
                if (strcmp(nm, "LIT") == 0) asm_lit(v);
                else if (strcmp(nm, "INT") == 0) asm_lit(mk_int(v));
                else if (strcmp(nm, "ARITY") == 0) asm_lit(mk_int(v));
                else asm_host((int)v);
            } else if (strcmp(nm, "ZBRANCH") == 0) {
                if (r0_tag(op) == T_WORD) { cell q = asm_zbranch_fwd(); raw_refs[raw_nrefs].patch = q; raw_refs[raw_nrefs].name = syms[word_id(op)]; raw_nrefs++; }
                else asm_zbranch((int)int_val(op));
            } else if (strcmp(nm, "BRANCH") == 0) {
                if (r0_tag(op) == T_WORD) { cell q = asm_branch_fwd(); raw_refs[raw_nrefs].patch = q; raw_refs[raw_nrefs].name = syms[word_id(op)]; raw_nrefs++; }
                else asm_branch((int)int_val(op));
            } else if (strcmp(nm, "CALL") == 0) {
                if (r0_tag(op) == T_WORD) { cell q = emit_call_fwd(); raw_refs[raw_nrefs].patch = q; raw_refs[raw_nrefs].name = syms[word_id(op)]; raw_nrefs++; }
                else asm_call((int)int_val(op));
            }
            continue;
        }
        if (strcmp(nm, "DUP") == 0) asm_dup();
        else if (strcmp(nm, "DROP") == 0) asm_drop();
        else if (strcmp(nm, "@") == 0) asm_fetch();
        else if (strcmp(nm, "!") == 0) asm_store();
        else if (strcmp(nm, "EXIT") == 0) asm_exit();
        else if (strcmp(nm, ">R") == 0) asm_toR();
        else if (strcmp(nm, "R>") == 0) asm_fromR();
        else if (strcmp(nm, "NONE") == 0) asm_lit(R0_NONE);
        else if (strcmp(nm, "ADD") == 0) asm_host(HOST_ADD);
        else if (strcmp(nm, "SUB") == 0) asm_host(HOST_SUB);
        else if (strcmp(nm, "MUL") == 0) asm_host(HOST_MUL);
        else if (strcmp(nm, "DIV") == 0) asm_host(HOST_DIV);
        else if (strcmp(nm, "MOD") == 0) asm_host(HOST_MOD);
        else if (strcmp(nm, "EQ") == 0) asm_host(HOST_EQ);
        else if (strcmp(nm, "NE") == 0) asm_host(HOST_NE);
        else if (strcmp(nm, "LT") == 0) asm_host(HOST_LT);
        else if (strcmp(nm, "GT") == 0) asm_host(HOST_GT);
        else if (strcmp(nm, "LE") == 0) asm_host(HOST_LE);
        else if (strcmp(nm, "GE") == 0) asm_host(HOST_GE);
        else if (strcmp(nm, "PRINT") == 0) asm_host(HOST_PRINT);
        else if (strcmp(nm, "ALLOC") == 0) asm_host(HOST_ALLOC);
        else fprintf(stderr, "r0_s1: unknown RAW mnemonic '%s'\n", nm);
    }
    for (int i = 0; i < raw_nrefs; i++) {
        cell a = raw_find_label(raw_refs[i].name);
        if (a < 0) { fprintf(stderr, "r0_s1: undefined RAW label '%s'\n", raw_refs[i].name); continue; }
        s1_set_mem(raw_refs[i].patch, a);
    }
    return entry;
}

static cell parse_block(parser_t *P) {
    P->pos++; /* '[' */
    cell tmp[256];
    int n = 0;
    for (;;) {
        skip_ws(P);
        char c = P->s[P->pos];
        if (c == ']') { P->pos++; break; }
        if (c == '\0') { P->err = 1; break; }
        /* `func` keyword: assign a func-site-id; parse spec normally, parse
         * body under the new site-id so nested blocks inherit it. */
        if (c != '[' && c != '-' && !(c >= '0' && c <= '9')) {
            int start = P->pos;
            while (P->s[P->pos] && !isspace((unsigned char)P->s[P->pos])
                   && P->s[P->pos] != '[' && P->s[P->pos] != ']') P->pos++;
            int len = P->pos - start;
            if (len == 4 && strncmp(P->s + start, "func", 4) == 0) {
                int sid = next_site++;
                tmp[n++] = intern("func");
                tmp[n++] = parse_form(P);                 /* spec */
                site_stack[site_depth++] = sid;
                tmp[n++] = parse_form(P);                 /* body (under sid) */
                site_depth--;
                continue;
            }
            if (len == 3 && strncmp(P->s + start, "raw", 3) == 0) {
                int arity = 0;
                /* optional integer arity before the assembly block */
                int save = P->pos;
                skip_ws(P);
                if (P->s[P->pos] >= '0' && P->s[P->pos] <= '9')
                    arity = (int)int_val(parse_int(P));
                else
                    P->pos = save;
                cell asm_block = parse_form(P);           /* [ instr... ] */
                cell entry = assemble_raw(asm_block);
                cell p = lalloc(2);
                M[p + RAW_ENTRY] = entry;
                M[p + RAW_ARITY] = (cell)arity;
                tmp[n++] = mk_raw(p);
                continue;
            }
            P->pos = start;
        }
        tmp[n++] = parse_form(P);
        if (n >= 256) { P->err = 1; break; }
    }
    cell b = make_block((cell)n);
    cell p = r0_untag(b);
    M[p] = (cell)n;
    M[p + BLK_SITE] = site_depth > 0 ? site_stack[site_depth - 1] : 0;
    for (int i = 0; i < n; i++) M[p + BLK_DATA + i] = tmp[i];
    return b;
}

static cell parse_form(parser_t *P) {
    skip_ws(P);
    char c = P->s[P->pos];
    if (c == '[') return parse_block(P);
    if (c >= '0' && c <= '9') return parse_int(P);
    if (c == '-' && isdigit((unsigned char)P->s[P->pos + 1])) return parse_int(P);
    return parse_word(P);
}

/* ============================ emitter helpers ========================== */

static void e_cell(cell c) { asm_lit(c); asm_fetch(); }
static void e_setc(cell c) { asm_lit(c); asm_store(); }
static void e_dup(void)    { asm_dup(); }
static void e_peek(void)   { asm_lit(REG_SP); asm_fetch(); asm_fetch(); }
static void e_pop_to(cell c) { e_peek(); asm_lit(c); asm_store(); asm_drop(); }
static void e_untag_ptr(void) { asm_dup(); asm_lit(16); asm_host(HOST_MOD); asm_host(HOST_SUB); }

/* a CALL whose callee will be patched later; returns the operand cell */
static cell emit_call_fwd(void) {
    asm_lit(0);
    cell cont = asm_here() - 1;
    asm_toR();
    asm_lit(0);
    cell callee_op = asm_here() - 1;
    asm_lit(REG_IP); asm_store();
    s1_set_mem(cont, asm_here());
    return callee_op;
}

/* a CALL whose callee is read at runtime from cell c (indirect CALL) */
static void emit_call_indirect(cell c) {
    asm_lit(0);
    cell cont = asm_here() - 1;
    asm_toR();
    e_cell(c);
    asm_lit(REG_IP); asm_store();
    s1_set_mem(cont, asm_here());
}

/* forward-call patch lists */
static cell to_subexpr[64];   static int n_subexpr;
static cell to_block_eval[16]; static int n_block_eval;

/* ========================== evaluator build ============================ */

static cell r_reduce, r_discard, r_lookup, r_set, r_append, r_mkctx, r_mkclosure;
static cell r_values, r_run_block, r_native, r_invoke_closure, r_return, r_invoke_raw, r_subexpr, r_block_eval;

/* REDUCE: [r1..rN, tagged-N] -> [r1] (or [NONE] if N==0) */
static void emit_reduce(void) {
    r_reduce = asm_here();
    e_pop_to(RV_T1);
    e_cell(RV_T1); asm_lit(16); asm_host(HOST_DIV); e_setc(RV_N);
    e_cell(RV_N); asm_lit(0); asm_host(HOST_EQ);
    cell jz = asm_zbranch_fwd();
    asm_lit(R0_NONE);
    asm_exit();
    asm_patch_here(jz);
    e_cell(RV_N); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_N);
    cell rloop = asm_here();
    e_cell(RV_N); asm_lit(0); asm_host(HOST_GT);
    cell jdone = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_N); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_N);
    asm_branch(rloop);
    asm_patch_here(jdone);
    asm_exit();
}

/* DISCARD: [r1..rN, tagged-N] -> (nothing) */
static void emit_discard(void) {
    r_discard = asm_here();
    e_pop_to(RV_T1);
    e_cell(RV_T1); asm_lit(16); asm_host(HOST_DIV); e_setc(RV_N);
    cell dloop = asm_here();
    e_cell(RV_N); asm_lit(0); asm_host(HOST_GT);
    cell jdone = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_N); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_N);
    asm_branch(dloop);
    asm_patch_here(jdone);
    asm_exit();
}

/* LOOKUP: (word -- value | -1 if unbound) via RV_CTX */
static void emit_lookup(void) {
    r_lookup = asm_here();
    e_pop_to(RV_T1);
    e_cell(RV_CTX);
    cell outer = asm_here();
    e_dup(); asm_lit(R0_NONE); asm_host(HOST_EQ);
    cell j_continue = asm_zbranch_fwd();
    asm_drop(); asm_lit(-1); asm_exit();
    asm_patch_here(j_continue);
    e_dup(); e_untag_ptr(); e_setc(RV_T2);
    e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T3);
    asm_lit(0); e_setc(RV_T4);
    cell inner = asm_here();
    e_cell(RV_T4); e_cell(RV_T3); asm_host(HOST_GE);
    cell j_search = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_T2); asm_fetch();
    asm_branch(outer);
    asm_patch_here(j_search);
    e_cell(RV_T2); asm_lit(3); asm_host(HOST_ADD);
    e_cell(RV_T4); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_fetch();
    e_cell(RV_T1); asm_host(HOST_EQ);
    cell j_nomatch = asm_zbranch_fwd();
    e_cell(RV_T2); asm_lit(4); asm_host(HOST_ADD);
    e_cell(RV_T4); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_fetch();
    e_setc(RV_T5);
    asm_drop();
    e_cell(RV_T5);
    asm_exit();
    asm_patch_here(j_nomatch);
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_T4);
    asm_branch(inner);
}

/* SET: (value -- value)  bind word (RV_WORD) to value, nearest-update */
static void emit_set(void) {
    r_set = asm_here();
    e_peek(); e_setc(RV_T6);
    e_cell(RV_CTX);
    cell outer = asm_here();
    e_dup(); asm_lit(R0_NONE); asm_host(HOST_EQ);
    cell j_continue = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_CTX); e_untag_ptr(); e_setc(RV_T2);
    e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T3);
    e_cell(RV_WORD);
    e_cell(RV_T2); asm_lit(3); asm_host(HOST_ADD);
    e_cell(RV_T3); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_store();
    e_cell(RV_T6);
    e_cell(RV_T2); asm_lit(4); asm_host(HOST_ADD);
    e_cell(RV_T3); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_store();
    e_cell(RV_T3); asm_lit(1); asm_host(HOST_ADD);
    e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD);
    asm_store();
    asm_exit();
    asm_patch_here(j_continue);
    e_dup(); e_untag_ptr(); e_setc(RV_T2);
    e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T3);
    asm_lit(0); e_setc(RV_T4);
    cell inner = asm_here();
    e_cell(RV_T4); e_cell(RV_T3); asm_host(HOST_GE);
    cell j_search = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_T2); asm_fetch();
    asm_branch(outer);
    asm_patch_here(j_search);
    e_cell(RV_T2); asm_lit(3); asm_host(HOST_ADD);
    e_cell(RV_T4); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_fetch();
    e_cell(RV_WORD); asm_host(HOST_EQ);
    cell j_nomatch = asm_zbranch_fwd();
    e_cell(RV_T6);
    e_cell(RV_T2); asm_lit(4); asm_host(HOST_ADD);
    e_cell(RV_T4); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_store();
    asm_drop();
    asm_exit();
    asm_patch_here(j_nomatch);
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_T4);
    asm_branch(inner);
}

/* APPEND: (value -- )  append (RV_WORD, value) to context RV_CHILD */
static void emit_append(void) {
    r_append = asm_here();
    e_cell(RV_CHILD); e_untag_ptr(); e_setc(RV_T2);
    e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T3);
    e_cell(RV_WORD);
    e_cell(RV_T2); asm_lit(3); asm_host(HOST_ADD);
    e_cell(RV_T3); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_store();
    e_cell(RV_T2); asm_lit(4); asm_host(HOST_ADD);
    e_cell(RV_T3); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_store();
    e_cell(RV_T3); asm_lit(1); asm_host(HOST_ADD);
    e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD);
    asm_store();
    asm_exit();
}

/* MKCTX: ( parent -- child-ctx )  allocate a fresh context */
static void emit_mkctx(void) {
    r_mkctx = asm_here();
    /* HOST_ALLOC does not align; allocate a 16-aligned cell count so the
     * tagged pointer (addr | T_CONTEXT) stays well-formed. */
    asm_lit((CTX_DATA + 2 * R0S1_CTX_CAP + 15) & ~15); asm_host(HOST_ALLOC); e_setc(RV_T4);
    e_cell(RV_T4); asm_store();                    /* M[addr] = parent */
    asm_lit(0); e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); asm_store();
    asm_lit(R0S1_CTX_CAP); e_cell(RV_T4); asm_lit(2); asm_host(HOST_ADD); asm_store();
    e_cell(RV_T4); asm_lit(T_CONTEXT); asm_host(HOST_ADD);
    asm_exit();
}

/* MKCLOSURE: ( spec body captured site-id -- closure ) */
static void emit_mkclosure(void) {
    r_mkclosure = asm_here();
    asm_lit(16); asm_host(HOST_ALLOC); e_setc(RV_T4);   /* 16-aligned, >= 4 cells */
    e_pop_to(RV_T5);  /* site-id */
    e_pop_to(RV_T1);  /* captured */
    e_pop_to(RV_T2);  /* body */
    e_pop_to(RV_T3);  /* spec */
    e_cell(RV_T3); e_cell(RV_T4); asm_store();                            /* spec */
    e_cell(RV_T2); e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); asm_store(); /* body */
    e_cell(RV_T1); e_cell(RV_T4); asm_lit(2); asm_host(HOST_ADD); asm_store(); /* captured */
    e_cell(RV_T5); e_cell(RV_T4); asm_lit(3); asm_host(HOST_ADD); asm_store(); /* site-id */
    e_cell(RV_T4); asm_lit(T_CLOSURE); asm_host(HOST_ADD);
    asm_exit();
}

/* VALUES: ( block -- [v1..vN, tagged-N] ) */
static void emit_values(void) {
    r_values = asm_here();
    e_untag_ptr(); e_setc(RV_T1);                    /* block_ptr */
    e_cell(RV_T1); asm_fetch(); e_setc(RV_T3);        /* count */
    e_cell(RV_CUR); asm_toR();
    e_cell(RV_END); asm_toR();
    e_cell(RV_BLK); asm_toR();
    e_cell(RV_T1); asm_lit(BLK_DATA); asm_host(HOST_ADD); e_setc(RV_CUR);
    e_cell(RV_T1); asm_lit(BLK_DATA); asm_host(HOST_ADD); e_cell(RV_T3); asm_host(HOST_ADD); e_setc(RV_END);
    e_cell(RV_T1); e_setc(RV_BLK);
    asm_lit(0); e_setc(RV_NVALS);
    cell vloop = asm_here();
    e_cell(RV_CUR); e_cell(RV_END); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    e_peek(); asm_lit(16); asm_host(HOST_EQ);
    cell j_err = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_NVALS); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_NVALS);
    asm_branch(vloop);
    asm_patch_here(j_err);
    asm_host(HOST_DUMP); asm_halt();
    asm_patch_here(j_done);
    asm_fromR(); e_setc(RV_BLK);
    asm_fromR(); e_setc(RV_END);
    asm_fromR(); e_setc(RV_CUR);
    e_cell(RV_NVALS); asm_lit(16); asm_host(HOST_MUL);
    asm_exit();
}

/* RUN-BLOCK: ( block -- result-set )  evaluate a block argument as code.
 * Saves RV_CUR/RV_END/RV_BLK, points them at the block, calls block-eval,
 * restores. */
static void emit_run_block(void) {
    r_run_block = asm_here();
    e_untag_ptr(); e_setc(RV_T1);                    /* block_ptr */
    e_cell(RV_T1); asm_fetch(); e_setc(RV_T3);        /* count */
    e_cell(RV_CUR); asm_toR();
    e_cell(RV_END); asm_toR();
    e_cell(RV_BLK); asm_toR();
    e_cell(RV_T1); asm_lit(BLK_DATA); asm_host(HOST_ADD); e_setc(RV_CUR);
    e_cell(RV_T1); asm_lit(BLK_DATA); asm_host(HOST_ADD);
    e_cell(RV_T3); asm_host(HOST_ADD); e_setc(RV_END);
    e_cell(RV_T1); e_setc(RV_BLK);
    to_block_eval[n_block_eval++] = emit_call_fwd();
    asm_fromR(); e_setc(RV_BLK);
    asm_fromR(); e_setc(RV_END);
    asm_fromR(); e_setc(RV_CUR);
    asm_exit();
}

/* NATIVE: (native -- result-set)  evaluate arity args, dispatch */
static void emit_native(void) {
    r_native = asm_here();
    asm_dup(); asm_lit(16); asm_host(HOST_DIV); e_setc(RV_NAT);
    asm_drop();
    /* print (id 14) */
    e_cell(RV_NAT); asm_lit(RN_PRINT); asm_host(HOST_EQ);
    cell j_not_print = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_call(r_reduce);
    asm_lit(16); asm_host(HOST_DIV);
    asm_host(HOST_PRINT);
    e_cell(RV_HOSTCALLS); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_HOSTCALLS);
    asm_lit(0);
    asm_exit();
    asm_patch_here(j_not_print);
    /* values (id 100) */
    e_cell(RV_NAT); asm_lit(RN_VALUES); asm_host(HOST_EQ);
    cell j_not_values = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_call(r_reduce);
    asm_call(r_values);
    asm_exit();
    asm_patch_here(j_not_values);
    /* either (id 101): arity 3 (cond then else) */
    e_cell(RV_NAT); asm_lit(RN_EITHER); asm_host(HOST_EQ);
    cell j_not_either = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();   /* cond */
    asm_call(r_reduce);
    to_subexpr[n_subexpr++] = emit_call_fwd();   /* then */
    asm_call(r_reduce);
    to_subexpr[n_subexpr++] = emit_call_fwd();   /* else */
    asm_call(r_reduce);
    e_pop_to(RV_T6);    /* else */
    e_pop_to(RV_T5);    /* then */
    e_pop_to(RV_T4);    /* cond */
    /* truthy = (cond != NONE) && (cond != 0) */
    e_cell(RV_T4); asm_lit(R0_NONE); asm_host(HOST_NE); e_setc(RV_T1);
    e_cell(RV_T4); asm_lit(0); asm_host(HOST_NE);
    e_cell(RV_T1); asm_host(HOST_MUL);
    cell j_false = asm_zbranch_fwd();
    e_cell(RV_T5);
    asm_call(r_run_block);                          /* eval then */
    asm_exit();
    asm_patch_here(j_false);
    e_cell(RV_T6);
    asm_call(r_run_block);                          /* eval else */
    asm_exit();
    asm_patch_here(j_not_either);
    /* do (id 102): arity 1 (block) */
    e_cell(RV_NAT); asm_lit(RN_DO); asm_host(HOST_EQ);
    cell j_not_do = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_call(r_reduce);
    asm_call(r_run_block);
    asm_exit();
    asm_patch_here(j_not_do);
    /* arithmetic: arity 2 */
    e_cell(RV_NAT); asm_toR();                 /* save native id across arg eval */
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_call(r_reduce);
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_call(r_reduce);
    asm_lit(16); asm_host(HOST_DIV);
    e_pop_to(RV_T1);
    asm_lit(16); asm_host(HOST_DIV);
    e_cell(RV_T1);
    asm_fromR(); e_setc(RV_NAT);               /* restore native id */
    {
        static const int op[9] = { HOST_ADD, HOST_SUB, HOST_MUL, HOST_DIV,
                                   HOST_EQ, HOST_LT, HOST_GT, HOST_LE, HOST_GE };
        static const int id[9] = { 0, 1, 2, 3, 6, 8, 9, 10, 11 };
        cell done[9];
        for (int k = 0; k < 9; k++) {
            e_cell(RV_NAT); asm_lit(id[k]); asm_host(HOST_EQ);
            cell j = asm_zbranch_fwd();
            asm_host(op[k]);
            done[k] = asm_branch_fwd();
            asm_patch_here(j);
        }
        for (int k = 0; k < 9; k++) asm_patch_here(done[k]);
    }
    asm_lit(16); asm_host(HOST_MUL);
    e_cell(RV_HOSTCALLS); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_HOSTCALLS);
    asm_lit(16);
    asm_exit();
}

/* INVOKE-CLOSURE: ( closure -- result-set )  real S1 activation */
static void emit_invoke_closure(void) {
    r_invoke_closure = asm_here();
    /* instrumentation: track max return/data depth */
    asm_lit(REG_RP); asm_fetch();
    e_cell(RV_RPMIN); asm_host(HOST_LT);
    cell j_rp = asm_zbranch_fwd();
    asm_lit(REG_RP); asm_fetch(); e_setc(RV_RPMIN);
    asm_patch_here(j_rp);
    asm_lit(REG_SP); asm_fetch();
    e_cell(RV_SPMIN); asm_host(HOST_LT);
    cell j_sp = asm_zbranch_fwd();
    asm_lit(REG_SP); asm_fetch(); e_setc(RV_SPMIN);
    asm_patch_here(j_sp);

    e_untag_ptr(); e_setc(RV_CLOSURE);
    e_cell(RV_CLOSURE); asm_fetch(); e_untag_ptr(); asm_fetch(); e_setc(RV_ARITY);
    e_cell(RV_CLOSURE); asm_toR();
    e_cell(RV_ARITY); asm_toR();
    /* evaluate arity arguments */
    asm_lit(0); e_setc(RV_T4);
    cell arg_loop = asm_here();
    e_cell(RV_T4); e_cell(RV_ARITY); asm_host(HOST_LT);
    cell j_args_done = asm_zbranch_fwd();
    e_cell(RV_T4); asm_toR();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_fromR(); e_setc(RV_T4);
    asm_call(r_reduce);
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_T4);
    asm_branch(arg_loop);
    asm_patch_here(j_args_done);
    asm_fromR(); e_setc(RV_ARITY);
    asm_fromR(); e_setc(RV_CLOSURE);
    /* child context (parent = captured) */
    e_cell(RV_CLOSURE); asm_lit(2); asm_host(HOST_ADD); asm_fetch();
    asm_call(r_mkctx);
    e_setc(RV_CHILD);
    /* bind params (i = arity-1 .. 0) */
    e_cell(RV_ARITY); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_T4);
    cell bind_loop = asm_here();
    e_cell(RV_T4); asm_lit(0); asm_host(HOST_GE);
    cell j_bind_done = asm_zbranch_fwd();
    e_pop_to(RV_T6);
    e_cell(RV_CLOSURE); asm_fetch(); e_untag_ptr(); asm_lit(BLK_DATA); asm_host(HOST_ADD);
    e_cell(RV_T4); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_WORD);
    e_cell(RV_T6);
    asm_call(r_append);
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_T4);
    asm_branch(bind_loop);
    asm_patch_here(j_bind_done);
    /* capture return address + caller RP baseline */
    asm_fetchR(); e_setc(RV_SIP);
    asm_lit(REG_RP); asm_fetch(); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_SRP);
    /* allocate + fill the activation frame (linked list in M) */
    asm_lit(16); asm_host(HOST_ALLOC); e_setc(RV_FNEW);
    e_cell(RV_FRAME); e_cell(RV_FNEW); asm_store();                          /* prev */
    e_cell(RV_CLOSURE); asm_lit(CLOSURE_SITE); asm_host(HOST_ADD); asm_fetch();
    e_cell(RV_FNEW); asm_lit(FRAME_SITE); asm_host(HOST_ADD); asm_store();   /* site */
    asm_lit(REG_SP); asm_fetch();
    e_cell(RV_FNEW); asm_lit(FRAME_SP); asm_host(HOST_ADD); asm_store();     /* SP */
    e_cell(RV_SRP); e_cell(RV_FNEW); asm_lit(FRAME_RP); asm_host(HOST_ADD); asm_store(); /* RP */
    e_cell(RV_SIP); e_cell(RV_FNEW); asm_lit(FRAME_IP); asm_host(HOST_ADD); asm_store(); /* IP */
    e_cell(RV_CTX); e_cell(RV_FNEW); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_store(); /* CTX */
    e_cell(RV_CUR); e_cell(RV_FNEW); asm_lit(FRAME_CUR); asm_host(HOST_ADD); asm_store(); /* CUR */
    e_cell(RV_END); e_cell(RV_FNEW); asm_lit(FRAME_END); asm_host(HOST_ADD); asm_store(); /* END */
    e_cell(RV_BLK); e_cell(RV_FNEW); asm_lit(FRAME_BLK); asm_host(HOST_ADD); asm_store(); /* BLK */
    e_cell(RV_FNEW); e_setc(RV_FRAME);
    /* enter body */
    e_cell(RV_CHILD); e_setc(RV_CTX);
    e_cell(RV_CLOSURE); asm_lit(CLOSURE_BODY); asm_host(HOST_ADD); asm_fetch(); e_untag_ptr(); e_setc(RV_BODY);
    e_cell(RV_BODY); asm_lit(BLK_DATA); asm_host(HOST_ADD); e_setc(RV_CUR);
    e_cell(RV_BODY); asm_lit(BLK_DATA); asm_host(HOST_ADD);
    e_cell(RV_BODY); asm_fetch(); asm_host(HOST_ADD); e_setc(RV_END);
    e_cell(RV_BODY); e_setc(RV_BLK);
    to_block_eval[n_block_eval++] = emit_call_fwd();
    /* normal return: restore caller state from frame, pop frame */
    e_cell(RV_FRAME); asm_lit(FRAME_END); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_END);
    e_cell(RV_FRAME); asm_lit(FRAME_CUR); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_CUR);
    e_cell(RV_FRAME); asm_lit(FRAME_BLK); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_BLK);
    e_cell(RV_FRAME); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_CTX);
    e_cell(RV_FRAME); asm_fetch(); e_setc(RV_FRAME);
    asm_exit();
}

/* RETURN: ( -- ) non-local definitional return. Reads the current block's
 * return-site-id, evaluates the return argument, finds the live activation
 * with that site-id, and restores its saved SP/RP/IP/RV state, jumping to its
 * return address. Intermediate activations' epilogues never run. */
static void emit_return(void) {
    r_return = asm_here();
    /* site_id = M[RV_BLK + BLK_SITE] */
    e_cell(RV_BLK); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_SITE);
    e_cell(RV_SITE); asm_lit(0); asm_host(HOST_EQ);
    cell j_ok = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                 /* return outside function */
    asm_patch_here(j_ok);
    /* evaluate the return argument (one sub-expression) -> [r.., N] */
    to_subexpr[n_subexpr++] = emit_call_fwd();
    /* preserve result set into RV_RES_BUF */
    e_pop_to(RV_T6);                                 /* RV_T6 = tagged N */
    e_cell(RV_T6); asm_lit(16); asm_host(HOST_DIV); e_setc(RV_N); /* RV_N = count */
    asm_lit(0); e_setc(RV_T4);
    cell pres_loop = asm_here();
    e_cell(RV_T4); e_cell(RV_N); asm_host(HOST_LT);
    cell j_pres_done = asm_zbranch_fwd();
    e_pop_to(RV_T5);
    e_cell(RV_T5); asm_lit(RV_RES_BUF); e_cell(RV_T4); asm_host(HOST_ADD); asm_store();
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_T4);
    asm_branch(pres_loop);
    asm_patch_here(j_pres_done);
    /* find the live activation with site_id (walk RV_FRAME chain) */
    e_cell(RV_FRAME); e_setc(RV_T2);
    cell find_loop = asm_here();
    e_cell(RV_T2); asm_lit(0); asm_host(HOST_EQ);
    cell j_nonzero = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                 /* no matching activation */
    asm_patch_here(j_nonzero);
    e_cell(RV_T2); asm_lit(FRAME_SITE); asm_host(HOST_ADD); asm_fetch();
    e_cell(RV_SITE); asm_host(HOST_EQ);
    cell j_nomatch = asm_zbranch_fwd();
    /* match: restore target state and jump */
    e_cell(RV_T2); asm_lit(FRAME_SP); asm_host(HOST_ADD); asm_fetch(); asm_lit(REG_SP); asm_store();
    e_cell(RV_N); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_T4);   /* re-place: i = N-1 */
    cell repl_loop = asm_here();
    e_cell(RV_T4); asm_lit(0); asm_host(HOST_GE);
    cell j_repl_done = asm_zbranch_fwd();
    asm_lit(RV_RES_BUF); e_cell(RV_T4); asm_host(HOST_ADD); asm_fetch();
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_T4);
    asm_branch(repl_loop);
    asm_patch_here(j_repl_done);
    e_cell(RV_T6);                                   /* push tagged N */
    e_cell(RV_T2); asm_lit(FRAME_RP); asm_host(HOST_ADD); asm_fetch(); asm_lit(REG_RP); asm_store();
    e_cell(RV_T2); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_CTX);
    e_cell(RV_T2); asm_lit(FRAME_CUR); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_CUR);
    e_cell(RV_T2); asm_lit(FRAME_END); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_END);
    e_cell(RV_T2); asm_lit(FRAME_BLK); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_BLK);
    e_cell(RV_T2); asm_fetch(); e_setc(RV_FRAME);     /* RV_FRAME = prev */
    e_cell(RV_T2); asm_lit(FRAME_IP); asm_host(HOST_ADD); asm_fetch(); asm_lit(REG_IP); asm_store();
    /* (control is now at the target's return address) */
    asm_patch_here(j_nomatch);
    e_cell(RV_T2); asm_fetch(); e_setc(RV_T2);         /* frame = prev */
    asm_branch(find_loop);
}

/* INVOKE-RAW: ( raw -- result-set )  call a first-class RAW S1 fragment.
 * Evaluates `arity` ordinary R0 arguments (each reduced to one value), CALLs
 * the fragment's entry, and returns whatever result set the fragment leaves.
 * The fragment itself is trusted/unsafe S1 code. */
static void emit_invoke_raw(void) {
    r_invoke_raw = asm_here();
    e_untag_ptr(); e_setc(RV_T1);                       /* ptr */
    e_cell(RV_T1); asm_fetch(); e_setc(RV_T5);          /* entry */
    e_cell(RV_T1); asm_lit(1); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T6); /* arity */
    asm_lit(0); e_setc(RV_T4);                          /* i = 0 */
    cell arg_loop = asm_here();
    e_cell(RV_T4); e_cell(RV_T6); asm_host(HOST_LT);    /* i < arity? */
    cell j_done = asm_zbranch_fwd();
    e_cell(RV_T4); asm_toR();
    e_cell(RV_T6); asm_toR();
    e_cell(RV_T5); asm_toR();
    to_subexpr[n_subexpr++] = emit_call_fwd();          /* eval one arg */
    asm_call(r_reduce);
    asm_fromR(); e_setc(RV_T5);
    asm_fromR(); e_setc(RV_T6);
    asm_fromR(); e_setc(RV_T4);
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_T4);
    asm_branch(arg_loop);
    asm_patch_here(j_done);
    emit_call_indirect(RV_T5);                          /* CALL entry */
    asm_exit();                                         /* return the result set */
}

/* SUBEXPR: evaluate one sub-expression at RV_CUR, advancing it */
static void emit_subexpr(void) {
    r_subexpr = asm_here();
    e_cell(RV_CUR); asm_fetch();
    e_cell(RV_CUR); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_CUR);
    asm_dup(); asm_lit(16); asm_host(HOST_MOD);

    /* WORD (tag 2) */
    asm_dup(); asm_lit(T_WORD); asm_host(HOST_EQ);
    cell j2 = asm_zbranch_fwd();
    asm_drop();
    /* func keyword? word == mk_word(0) */
    asm_dup(); asm_lit(mk_word(FUNC_SYM)); asm_host(HOST_EQ);
    cell j_notfunc = asm_zbranch_fwd();
    asm_drop();
    to_subexpr[n_subexpr++] = emit_call_fwd();  /* spec */
    asm_call(r_reduce);
    to_subexpr[n_subexpr++] = emit_call_fwd();  /* body */
    asm_call(r_reduce);
    e_pop_to(RV_T2);                             /* body */
    e_pop_to(RV_T3);                             /* spec */
    e_cell(RV_T3);                               /* spec */
    e_cell(RV_T2);                               /* body */
    e_cell(RV_CTX);                              /* captured */
    e_cell(RV_T2); e_untag_ptr(); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch(); /* site-id */
    asm_call(r_mkclosure);
    asm_lit(16);
    asm_exit();
    asm_patch_here(j_notfunc);
    /* return keyword? word == mk_word(1) */
    asm_dup(); asm_lit(mk_word(RETURN_SYM)); asm_host(HOST_EQ);
    cell j_notreturn = asm_zbranch_fwd();
    asm_drop();
    asm_call(r_return);                          /* non-local return (jumps away) */
    asm_exit();                                  /* unreachable safety */
    asm_patch_here(j_notreturn);
    asm_call(r_lookup);
    asm_dup(); asm_lit(-1); asm_host(HOST_EQ);
    cell j_err = asm_zbranch_fwd();
    asm_drop(); asm_host(HOST_DUMP); asm_halt();
    asm_patch_here(j_err);
    asm_dup(); asm_lit(16); asm_host(HOST_MOD);
    /* native (tag 9) */
    asm_dup(); asm_lit(T_NATIVE); asm_host(HOST_EQ);
    cell j_notnat = asm_zbranch_fwd();
    asm_drop();
    asm_call(r_native);
    asm_exit();
    asm_patch_here(j_notnat);
    /* closure (tag 8) */
    asm_dup(); asm_lit(T_CLOSURE); asm_host(HOST_EQ);
    cell j_notclosure = asm_zbranch_fwd();
    asm_drop();
    asm_call(r_invoke_closure);
    asm_exit();
    asm_patch_here(j_notclosure);
    /* raw (tag 10) */
    asm_dup(); asm_lit(T_RAW); asm_host(HOST_EQ);
    cell j_notraw = asm_zbranch_fwd();
    asm_drop();
    asm_call(r_invoke_raw);
    asm_exit();
    asm_patch_here(j_notraw);
    asm_drop();
    asm_lit(16);
    asm_exit();
    asm_patch_here(j2);

    /* SET (tag 3) */
    asm_dup(); asm_lit(T_SET); asm_host(HOST_EQ);
    cell j3 = asm_zbranch_fwd();
    asm_drop(); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_WORD);
    e_cell(RV_WORD); asm_toR();                 /* save target across RHS eval */
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_fromR(); e_setc(RV_WORD);               /* restore target */
    asm_call(r_reduce);
    asm_call(r_set);
    asm_lit(16);
    asm_exit();
    asm_patch_here(j3);

    /* GET (tag 4) */
    asm_dup(); asm_lit(T_GET); asm_host(HOST_EQ);
    cell j4 = asm_zbranch_fwd();
    asm_drop(); asm_lit(2); asm_host(HOST_SUB);
    asm_call(r_lookup);
    asm_lit(16);
    asm_exit();
    asm_patch_here(j4);

    /* LIT (tag 5) */
    asm_dup(); asm_lit(T_LIT); asm_host(HOST_EQ);
    cell j5 = asm_zbranch_fwd();
    asm_drop(); asm_lit(3); asm_host(HOST_SUB);
    asm_lit(16);
    asm_exit();
    asm_patch_here(j5);

    /* self-evaluating literals */
    asm_drop();
    asm_lit(16);
    asm_exit();
}

/* BLOCK-EVAL: evaluate block at RV_CUR..RV_END, leaving [r..,N] (subroutine) */
static void emit_block_eval(void) {
    r_block_eval = asm_here();
    e_cell(RV_CUR); e_cell(RV_END); asm_host(HOST_LT);
    cell j_empty = asm_zbranch_fwd();
    cell ltop = asm_here();
    asm_call(r_subexpr);
    e_cell(RV_CUR); e_cell(RV_END); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    asm_call(r_discard);
    asm_branch(ltop);
    asm_patch_here(j_done);
    asm_exit();
    asm_patch_here(j_empty);
    asm_lit(R0_NONE); asm_lit(16);
    asm_exit();
}

/* MAIN: top-level entry (CALL block-eval; HALT) */
static void emit_main(void) {
    main_entry = asm_here();
    asm_call(r_block_eval);
    asm_halt();
}

/* ============================ public API =============================== */

cell r0_s1_init(void) {
    hp = R0S1_HEAP_BASE;
    nsyms = 0;
    n_subexpr = 0; n_block_eval = 0;
    next_site = 1;      /* func-site-ids start at 1; 0 = "no enclosing func" */
    site_depth = 0;

    asm_reset();
    code_begin = asm_here();
    emit_reduce();
    emit_discard();
    emit_lookup();
    emit_set();
    emit_append();
    emit_mkctx();
    emit_mkclosure();
    emit_values();
    emit_run_block();
    emit_native();
    emit_invoke_closure();
    emit_return();
    emit_invoke_raw();
    emit_subexpr();
    emit_block_eval();
    emit_main();
    code_end = asm_here();

    for (int i = 0; i < n_subexpr; i++) s1_set_mem(to_subexpr[i], r_subexpr);
    for (int i = 0; i < n_block_eval; i++) s1_set_mem(to_block_eval[i], r_block_eval);

    /* preload the global environment ("func"/"return"/"raw" first => sym 0/1/2) */
    intern("func");
    intern("return");
    intern("raw");
    global_ctx = make_context(R0_NONE, 48);
    bind(global_ctx, intern("+"),  mk_native(RN_ADD));
    bind(global_ctx, intern("-"),  mk_native(RN_SUB));
    bind(global_ctx, intern("*"),  mk_native(RN_MUL));
    bind(global_ctx, intern("/"),  mk_native(RN_DIV));
    bind(global_ctx, intern("="),  mk_native(RN_EQ));
    bind(global_ctx, intern("<"),  mk_native(RN_LT));
    bind(global_ctx, intern(">"),  mk_native(RN_GT));
    bind(global_ctx, intern("<="), mk_native(RN_LE));
    bind(global_ctx, intern(">="), mk_native(RN_GE));
    bind(global_ctx, intern("print"), mk_native(RN_PRINT));
    bind(global_ctx, intern("values"), mk_native(RN_VALUES));
    bind(global_ctx, intern("either"), mk_native(RN_EITHER));
    bind(global_ctx, intern("do"), mk_native(RN_DO));

    return main_entry;
}

cell r0_s1_parse(const char *src, int *err) {
    parser_t P; P.s = src; P.pos = 0; P.err = 0;
    *err = 0;
    site_depth = 0;
    skip_ws(&P);
    if (P.s[P.pos] == '[') { cell b = parse_block(&P); if (P.err) *err = 1; return b; }
    cell tmp[256]; int n = 0;
    while (P.s[P.pos] && !P.err) {
        skip_ws(&P);
        if (!P.s[P.pos]) break;
        tmp[n++] = parse_form(&P);
        if (n >= 256) { P.err = 1; break; }
    }
    cell b = make_block((cell)n);
    cell p = r0_untag(b);
    M[p] = (cell)n;
    M[p + BLK_SITE] = 0;
    for (int i = 0; i < n; i++) M[p + BLK_DATA + i] = tmp[i];
    if (P.err) *err = 1;
    return b;
}

int r0_s1_run(cell block) {
    cell bp = r0_untag(block);
    M[RV_CUR] = bp + BLK_DATA;
    M[RV_END] = bp + BLK_DATA + M[bp];
    M[RV_BLK] = bp;
    M[RV_CTX] = global_ctx;
    M[RV_FRAME] = 0;
    M[RV_HOSTCALLS] = 0;
    M[RV_RPMIN] = 65535;
    M[RV_SPMIN] = 65535;

    s1_reset();
    ip_start = s1_mem(REG_IP);
    sp_start = s1_mem(REG_SP);
    rp_start = s1_mem(REG_RP);
    s1_run(main_entry);
    ip_end = s1_mem(REG_IP);
    sp_end = s1_mem(REG_SP);
    rp_end = s1_mem(REG_RP);

    cell arity = s1_top();
    return (int)(arity / 16);
}

cell r0_s1_result(int i, int N) {
    cell sp = s1_mem(REG_SP);
    return s1_mem(sp + N - i);
}

cell r0_s1_ip_start(void) { return ip_start; }
cell r0_s1_ip_end(void)   { return ip_end; }
cell r0_s1_sp_start(void) { return sp_start; }
cell r0_s1_sp_end(void)   { return sp_end; }
cell r0_s1_rp_start(void) { return rp_start; }
cell r0_s1_rp_end(void)   { return rp_end; }
cell r0_s1_rp_min(void)   { return M[RV_RPMIN]; }
cell r0_s1_sp_min(void)   { return M[RV_SPMIN]; }
cell r0_s1_host_calls(void){ return M[RV_HOSTCALLS]; }
cell r0_s1_code_size(void) { return code_end - code_begin; }
