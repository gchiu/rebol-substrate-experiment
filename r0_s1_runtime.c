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
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ============================= loader state ============================ */

static const char *syms[512];            /* interner: sym_id -> spelling */
static int nsyms;
static cell global_ctx;                  /* tagged CONTEXT value */
static cell main_entry;                  /* top-level S1 entry point */
static cell main_halt_ip;                /* REG_IP value after a NORMAL run */
static int g_seed_datatypes;             /* M3: enable bootstrap heap seeding */
static int stack_sentry_fired;           /* stack sentry: last run violated bounds */

/* instrumentation */
static cell ip_start, ip_end, sp_start, sp_end, rp_start, rp_end;
static cell code_begin, code_end;

/* G1: forward declarations for the managed-heap entry points referenced by the
 * RAW assembler's symbolic CALL targets (alloc/collect/lookup). Defined later
 * by emit_gc/emit_alloc/emit_lookup. */
static cell r_alloc, r_collect, r_lookup;

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Why the last r0_s1_parse failed (R0S1_PARSE_*; see r0_s1.h). Resource
 * failures are recorded where they happen (intern, loader allocation, the
 * site table, form limits); r0_s1_parse classifies any other failure as
 * SYNTAX. The first failure recorded wins. */
static int parse_fail;
static void parse_fail_set(int kind) { if (!parse_fail) parse_fail = kind; }

/* The symbol table has a fixed capacity (syms[]). When it is full, intern
 * records R0S1_PARSE_SYMBOL_TABLE_FULL and returns a placeholder word; the
 * parse then fails and is rolled back, so the placeholder is never run. */
#define SYM_CAP ((int)(sizeof syms / sizeof syms[0]))
static cell intern(const char *name) {
    for (int i = 0; i < nsyms; i++)
        if (strcmp(syms[i], name) == 0) return mk_word(i);
    char *copy = nsyms < SYM_CAP ? dup_str(name) : 0;
    if (!copy) { parse_fail_set(R0S1_PARSE_SYMBOL_TABLE_FULL); return mk_word(0); }
    syms[nsyms] = copy;
    return mk_word(nsyms++);
}

/* Loader-heap allocation. Returns -1 when the loader heap is exhausted; every
 * caller checks, so no one writes through the failure value. */
static cell lalloc(int n) {
    cell hp = M[GC_LOADER_HP];
    cell a = (hp + 15) & ~15L;
    if (a + n > R0S1_HEAP_LIMIT) { fprintf(stderr, "r0_s1: heap exhausted\n"); return -1; }
    M[GC_LOADER_HP] = a + n;
    return a;
}

/* ============================= loader: objects ========================= */

/* R0_NONE (after recording R0S1_PARSE_LOADER_EXHAUSTED) if the loader heap is
 * exhausted; callers must check before writing into the block. */
static cell make_block(cell cap) {
    cell p = lalloc(2 + (int)cap);   /* [count, site_id, elems...] */
    if (p < 0) { parse_fail_set(R0S1_PARSE_LOADER_EXHAUSTED); return R0_NONE; }
    M[p] = 0;
    M[p + BLK_SITE] = 0;
    return mk_block(p);
}
static cell make_context(cell parent, cell cap) {
    cell p = lalloc(CTX_DATA + 3 * (int)cap);   /* data(2*cap) + hash(cap) */
    if (p < 0) return R0_NONE;                   /* only r0_s1_init calls this */
    M[p + CTX_PARENT] = parent;
    M[p + CTX_COUNT] = 0;
    M[p + CTX_CAP] = cap;
    cell ho = CTX_DATA + 2 * (int)cap;
    for (cell i = 0; i < cap; i++) M[p + ho + i] = HASH_EMPTY;
    return mk_context(p);
}
static void bind(cell ctx, cell word, cell value) {
    cell p = r0_untag(ctx);
    cell n = M[p + CTX_COUNT];
    M[p + CTX_DATA + 2 * n] = word;
    M[p + CTX_DATA + 2 * n + 1] = value;
    M[p + CTX_COUNT] = n + 1;
    /* P5: maintain the hash index (linear probe; index is skipped once full) */
    cell cap = M[p + CTX_CAP];
    cell ho = CTX_DATA + 2 * cap;
    if (n < cap) {
        cell id = word_id(word);
        cell h = id % cap;
        for (cell k = 0; k < cap; k++) {
            cell idx = (h + k) % cap;
            cell e = M[p + ho + idx];
            if (e == HASH_EMPTY || e / 256 == id) { M[p + ho + idx] = id * 256 + n; break; }
        }
    }
}

/* ============================== parser ================================= */

typedef struct { const char *s; int pos; int err; } parser_t;

/* func-site-id assignment and the lexical enclosing-site stack (parse/load) */
static int site_stack[64];
static int site_depth;
static int next_site;

/* Escape-time binding law (parse/load): number of literal FUNC body scopes open
 * at the current parse point, and the dependency flag of the most recently
 * parsed block. A block is activation-dependent if its executable tree (outside
 * its own nested literal func scopes) carries a T_BOUND that reaches its origin
 * activation or a RETURN. */
static int parse_func_depth;
static int last_block_dep;

/* Escape-time binding law enforcement subroutines (emitted S1; defined later). */
static cell r_esc_any, r_esc_eq, r_esc_capture, r_esc_results, r_esc_same, r_esc_anc, r_esc_transport;

/* CLOSURE_SITE of a closure made from a block whose lexical origin activation
 * is dead. Real sites are >= 0, so no block's BLK_SITE ever equals it and the
 * LOAD-LEX guard fail-stops any T_BOUND reference in such a body. A multiple
 * of 16, so the raw FRAME_SITE cell still reads as an INT to any scan. */
#define DEAD_SITE (-16)

/* FIB-OPT-P4: lexical binding scope stack (parse/load). Each scope holds the
 * word ids of the enclosing function's PARAMETERS, stored in slot order (the
 * runtime bind loop appends params in reverse spec order, so slot 0 is the
 * last spec word). Locals are deliberately NOT tracked: r_set assigns local
 * slots in execution order, so their slots are not statically sound (see
 * FIB-OPT-P4-LEX.md). References resolve innermost-first; depth = index from
 * the top. */
#define MAX_LEX_DEPTH 64
static cell lex_names[MAX_LEX_DEPTH][R0S1_CTX_CAP];
static int  lex_count[MAX_LEX_DEPTH];
static int  lex_depth;

static void lex_push(void) { if (lex_depth < MAX_LEX_DEPTH) lex_count[lex_depth++] = 0; }
static void lex_pop(void)  { if (lex_depth > 0) lex_depth--; }
static void lex_add(cell id) {
    int d = lex_depth - 1;
    if (d < 0 || lex_count[d] >= R0S1_CTX_CAP) return;
    lex_names[d][lex_count[d]++] = id;
}
static int lex_resolve(cell id, int *depth, int *slot) {
    for (int d = 0; d < lex_depth; d++) {
        int si = lex_depth - 1 - d;
        for (int s = 0; s < lex_count[si]; s++)
            if (lex_names[si][s] == id) { *depth = d; *slot = s; return 1; }
    }
    return 0;
}

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
    {
        cell id = word_id(intern(buf));
        /* FIB-OPT-P4: resolve a plain reference to an enclosing parameter.
         * Reserved keywords (func/return/raw = ids 0/1/2) are never resolved,
         * and only parameters are tracked, so this can only fast-path a name
         * whose slot is statically sound. */
        if (id >= 3 && lex_depth > 0) {
            int depth, slot;
            if (lex_resolve(id, &depth, &slot)) return mk_bound(depth, slot);
        }
        return mk_word(id);
    }
}

static cell parse_form(parser_t *P);
static cell emit_call_fwd(void);   /* forward decl (defined in the emitter section) */

/* ================= MASM: S1 macroassembler (loader/toolchain) ==============
 * Translates symbolic S1 assembly (mnemonics + operands + labels) into S1
 * cells. This is pure toolchain: it knows the frozen opcodes and a few
 * derived conveniences, and knows NOTHING about R0 control constructs
 * (break/throw or any reified control effect).
 *
 * The Glon source spelling is `masm [...]`; `raw` is the legacy alias for the
 * same path (the internal C identifiers below still say "raw"). */

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
    { "RV_CLOSURE", RV_CLOSURE }, { "RV_CHILD", RV_CHILD },
    { "FRAME_PREV", FRAME_PREV }, { "FRAME_SITE_ID", FRAME_SITE },
    { "FRAME_SAVED_SP", FRAME_SP }, { "FRAME_SAVED_RP", FRAME_RP },
    { "FRAME_SAVED_IP", FRAME_IP }, { "FRAME_SAVED_CTX", FRAME_CTX },
    { "FRAME_SAVED_CUR", FRAME_CUR }, { "FRAME_SAVED_END", FRAME_END },
    { "FRAME_SAVED_BLK", FRAME_BLK },
    { "SCRATCH_A", RV_SCRATCH_A }, { "SCRATCH_B", RV_SCRATCH_B },
    { "SCRATCH_C", RV_SCRATCH_C }, { "SCRATCH_D", RV_SCRATCH_D },
    { "SCRATCH_E", RV_SCRATCH_E }, { "SCRATCH_F", RV_SCRATCH_F },
    { "BUILTIN_BASE", BUILTIN_BASE }, { "GC_META", GC_META },
    { "T_USER", T_USER }, { "GC_KIND_USER", GC_KIND_USER },
    { "T_STRING", T_STRING }, { "GC_KIND_STRING", GC_KIND_STRING },
    { "T_BLOCK", T_BLOCK }, { "GC_KIND_BLOCK", GC_KIND_BLOCK },
    { "G1_OUT", G1_OUT }, { "G1_OUT_DATA", G1_OUT_DATA },
    { "G1_VIS", G1_VIS }, { "G1_VIS_DATA", G1_VIS_DATA },
    { "HOST_PUTCHAR", HOST_PUTCHAR },   /* frozen S1 byte output (strings.glon s/print) */
    /* M1 multitasking world (m1_layout.h): task records, arena, scratch */
    { "M1_MAIN_ENTRY_CELL", M1_MAIN_ENTRY_CELL },
    { "M1_CUR_TASK", M1_CUR_TASK }, { "M1_SCHED_REC", M1_SCHED_REC },
    { "M1_CURSOR", M1_CURSOR },
    { "M1_S0", M1_S0 }, { "M1_S1", M1_S1 }, { "M1_S2", M1_S2 }, { "M1_S3", M1_S3 },
    { "M1_S4", M1_S4 }, { "M1_S5", M1_S5 }, { "M1_S6", M1_S6 }, { "M1_S7", M1_S7 },
    { "M1_S8", M1_S8 },
    { "M1_TASK_TABLE", M1_TASK_TABLE }, { "M1_ARENA_BASE", M1_ARENA_BASE },
    { "M1_TASK_CELLS", M1_TASK_CELLS }, { "M1_DS_OFF", M1_DS_OFF },
    { "M1_RS_OFF", M1_RS_OFF }, { "M1_WRAPPER_DELTA", M1_WRAPPER_DELTA },
    { "TREC_IP", TREC_IP }, { "TREC_SP", TREC_SP }, { "TREC_RP", TREC_RP },
    { "TREC_CUR", TREC_CUR }, { "TREC_END", TREC_END }, { "TREC_CTX", TREC_CTX },
    { "TREC_BLK", TREC_BLK }, { "TREC_FRAME", TREC_FRAME }, { "TREC_STATE", TREC_STATE },
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
                if (r0_tag(op) == T_WORD) {
                    const char *cn = syms[word_id(op)];
                    /* symbolic managed-heap entry points (G1 STRING! primitives) */
                    if (strcmp(cn, "alloc") == 0) asm_call(r_alloc);
                    else if (strcmp(cn, "collect") == 0) asm_call(r_collect);
                    else if (strcmp(cn, "lookup") == 0) asm_call(r_lookup);
                    else if (strcmp(cn, "ESC_ANY") == 0) asm_call(r_esc_any);
                    else if (strcmp(cn, "ESC_TRANSPORT") == 0) asm_call(r_esc_transport);
                    else { cell q = emit_call_fwd(); raw_refs[raw_nrefs].patch = q; raw_refs[raw_nrefs].name = cn; raw_nrefs++; }
                }
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

/* String literal: read `"..."` and produce the byte-list block [ b0 b1 ... ]
 * (one mk_int per UTF-8 byte).  The enclosing form loop pairs this with the
 * `mk-string` word so a quoted source literal becomes the runtime form
 * `mk-string [bytes]`, which evaluates to the existing managed STRING!. */
static cell parse_string_literal(parser_t *P) {
    P->pos++;                            /* opening '"' */
    cell bytes[512]; int n = 0;
    while (P->s[P->pos] && P->s[P->pos] != '"') {
        if (n >= 512) { parse_fail_set(R0S1_PARSE_TOO_LARGE); P->err = 1; break; }
        bytes[n++] = mk_int((cell)(unsigned char)P->s[P->pos]);
        P->pos++;
    }
    if (P->s[P->pos] == '"') P->pos++; else P->err = 1;
    cell b = make_block((cell)n);
    if (b == R0_NONE) { P->err = 1; return R0_NONE; }
    cell p = r0_untag(b);
    M[p] = (cell)n;
    M[p + BLK_SITE] = 0;
    for (int i = 0; i < n; i++) M[p + BLK_DATA + i] = bytes[i];
    return b;
}

static cell parse_block(parser_t *P, int is_body) {
    P->pos++; /* '[' */
    cell tmp[512];
    int n = 0;
    int base_fd = parse_func_depth;
    int dep = 0;
    for (;;) {
        skip_ws(P);
        char c = P->s[P->pos];
        if (c == ']') { P->pos++; break; }
        if (c == '\0') { P->err = 1; break; }
        if (parse_fail) { P->err = 1; break; }   /* a resource failure ends the parse */
        /* quoted string literal: expand to `mk-string [bytes]` (two forms). */
        if (c == '"') {
            tmp[n++] = intern("mk-string");
            tmp[n++] = parse_string_literal(P);
            if (n >= 512) { parse_fail_set(R0S1_PARSE_TOO_LARGE); P->err = 1; break; }
            continue;
        }
        /* `func` keyword: assign a func-site-id; parse spec normally, parse
         * body under the new site-id so nested blocks inherit it. */
        if (c != '[' && c != '-' && !(c >= '0' && c <= '9')) {
            int start = P->pos;
            while (P->s[P->pos] && !isspace((unsigned char)P->s[P->pos])
                   && P->s[P->pos] != '[' && P->s[P->pos] != ']') P->pos++;
            int len = P->pos - start;
            if (len == 4 && strncmp(P->s + start, "func", 4) == 0) {
                int sid = next_site++;
                if (sid < SITE_PARENT_CAP)
                    M[SITE_PARENT_BASE + sid] = site_depth > 0 ? site_stack[site_depth - 1] : 0;
                else {
                    parse_fail_set(R0S1_PARSE_SITE_TABLE_FULL);
                    P->err = 1;                       /* site table exhausted */
                }
                tmp[n++] = intern("func");
                /* spec (param block): its words NAME the new parameters, so
                 * they must never be lexically resolved against an enclosing
                 * func's same-named parameter (that would turn `x` into a
                 * T_BOUND, drop it from the new scope, and make the body's `x`
                 * silently read the OUTER x). */
                int save_lex_depth = lex_depth;
                lex_depth = 0;
                cell spec = parse_form(P);
                lex_depth = save_lex_depth;
                /* Only a literal body block is this func's own lexical scope.
                 * A computed body (`func [] :b`, `func [] block-at rows i`) is
                 * an ordinary expression in the ENCLOSING scope; parsing it
                 * under the new scope would give its words wrong depths. */
                skip_ws(P);
                if (P->s[P->pos] != '[') {
                    tmp[n++] = spec;
                    continue;
                }
                lex_push();                               /* new lexical scope */
                if (r0_tag(spec) == T_BLOCK) {
                    /* collect the spec's parameter words.  The bind loop in
                     * invoke_closure appends params in REVERSE spec order
                     * (slot 0 = spec[arity-1], ...), so add them in reverse
                     * order here so the slot indices match the runtime. */
                    cell sp = r0_untag(spec);
                    int cnt = (int)M[sp];
                    for (int i = cnt - 1; i >= 0; i--) {
                        cell e = M[sp + BLK_DATA + i];
                        if (r0_tag(e) == T_WORD) lex_add(word_id(e));
                    }
                }
                tmp[n++] = spec;
                site_stack[site_depth++] = sid;
                parse_func_depth++;                       /* the body's own scope */
                tmp[n++] = parse_block(P, 1);             /* literal body (under scope) */
                parse_func_depth--;
                /* A nested literal func whose body reaches an ENCLOSING
                 * activation's scope makes the enclosing block
                 * activation-dependent: executing it elsewhere would create the
                 * closure against a foreign/dead origin. */
                if (last_block_dep) dep = 1;
                site_depth--;
                lex_pop();
                continue;
            }
            /* `masm` (canonical) and `raw` (compatibility alias) both enter the
             * same S1 macroassembler path; nothing below differs. */
            if ((len == 3 && strncmp(P->s + start, "raw", 3) == 0) ||
                (len == 4 && strncmp(P->s + start, "masm", 4) == 0)) {
                int arity = 0;
                /* optional integer arity before the assembly block */
                int save = P->pos;
                skip_ws(P);
                if (P->s[P->pos] >= '0' && P->s[P->pos] <= '9')
                    arity = (int)int_val(parse_int(P));
                else
                    P->pos = save;
                /* The RAW source block is dead once assembled (its mnemonics
                 * are translated into emitted code); reclaim its loader-heap
                 * space so self-contained demos that re-define the M1 library
                 * do not exhaust the loader heap.  Only the 2-cell callable
                 * [entry, arity] needs to survive. */
                cell save_lhp = M[GC_LOADER_HP];
                cell asm_block = parse_form(P);           /* [ instr... ] */
                cell entry = assemble_raw(asm_block);
                M[GC_LOADER_HP] = save_lhp;
                cell p = lalloc(2);
                if (p < 0) {
                    parse_fail_set(R0S1_PARSE_LOADER_EXHAUSTED);
                    P->err = 1;
                    break;
                }
                M[p + RAW_ENTRY] = entry;
                M[p + RAW_ARITY] = (cell)arity;
                tmp[n++] = mk_raw(p);
                continue;
            }
            P->pos = start;
        }
        {
            cell form = parse_form(P);
            /* Escape-time binding law: mark the block activation-dependent if
             * this form is a T_BOUND that reaches the origin activation (rather
             * than a scope opened by a literal func nested inside this block),
             * a RETURN (whose target is found by BLK_SITE), or a nested block
             * that is itself dependent. */
            if (r0_tag(form) == T_BOUND) {
                int thr = (parse_func_depth - base_fd) + (is_body ? 1 : 0);
                if ((int)bound_depth(form) >= thr) dep = 1;
            } else if (form == mk_word(RETURN_SYM)) {
                dep = 1;
            } else if (r0_tag(form) == T_BLOCK) {
                if (last_block_dep) dep = 1;
            }
            tmp[n++] = form;
        }
        if (n >= 512) { parse_fail_set(R0S1_PARSE_TOO_LARGE); P->err = 1; break; }
    }
    cell b = make_block((cell)n);
    if (b == R0_NONE) { P->err = 1; last_block_dep = 0; return R0_NONE; }
    cell p = r0_untag(b);
    M[p] = (cell)n;
    {
        cell site = site_depth > 0 ? site_stack[site_depth - 1] : 0;
        M[p + BLK_SITE] = dep ? BLK_SITE_DEP(site) : site;
    }
    last_block_dep = dep;
    for (int i = 0; i < n; i++) M[p + BLK_DATA + i] = tmp[i];
    return b;
}

static cell parse_form(parser_t *P) {
    skip_ws(P);
    char c = P->s[P->pos];
    if (c == '[') return parse_block(P, 0);
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

/* Escape-law: a raw BLK_SITE on the data stack may be dep-encoded (negative).
 * Decode it in place to the non-negative site id. */
static void emit_decode_site(void) {
    asm_dup(); asm_lit(0); asm_host(HOST_LT);       /* raw < 0 ? */
    cell j_nonneg = asm_zbranch_fwd();
    asm_neg(); asm_lit(1); asm_host(HOST_SUB);      /* -raw - 1 */
    asm_patch_here(j_nonneg);
}

/* Escape-law enforcement routines are emitted AFTER all existing evaluator
 * code (so their addresses do not shift existing routines); the transport
 * hooks reach them through forward-patched CALL sites. */
typedef struct { cell patch; cell *slot; } esc_ref_t;
static esc_ref_t esc_refs[64];
static int n_esc_refs;
static void emit_esc_ref(cell *slot) {
    esc_refs[n_esc_refs].patch = emit_call_fwd();
    esc_refs[n_esc_refs].slot = slot;
    n_esc_refs++;
}

/* --- FIB-PROFILE-P1 profiler emitters (compiled only under -DR0_S1_PROFILE).
 * Each helper emits pure S1 code above the frozen substrate; none changes GLON
 * semantics. In a baseline build the helpers emit nothing. */
#ifdef R0_S1_PROFILE
static void pf_incr(cell c)            { e_cell(c); asm_lit(1); asm_host(HOST_ADD); e_setc(c); }
static void pf_incr_by(cell c, cell s) { e_cell(c); e_cell(s); asm_host(HOST_ADD); e_setc(c); }
static void pf_putc(int ch)            { asm_lit(ch); asm_host(HOST_PUTCHAR); }
/* emit a runtime-gated (PF_TRACE != 0) trace-block prologue; returns a label
 * patched at the end of the block by pf_trace_end() */
static cell pf_trace_begin(void) {
    e_cell(PF_TRACE); asm_lit(0); asm_host(HOST_NE);
    cell skip = asm_zbranch_fwd();   /* PF_TRACE == 0 -> skip */
    return skip;
}
static void pf_trace_end(cell skip) { asm_patch_here(skip); }
/* increment PF_NATIVE and the per-native counter matching RV_NAT */
static void pf_native_count(void) {
    pf_incr(PF_NATIVE);
    e_cell(RV_NAT); asm_lit(RN_LE); asm_host(HOST_EQ);
    cell j1 = asm_zbranch_fwd();
    pf_incr(PF_NAT_LE);
    cell d1 = asm_branch_fwd();
    asm_patch_here(j1);
    e_cell(RV_NAT); asm_lit(RN_SUB); asm_host(HOST_EQ);
    cell j2 = asm_zbranch_fwd();
    pf_incr(PF_NAT_SUB);
    cell d2 = asm_branch_fwd();
    asm_patch_here(j2);
    e_cell(RV_NAT); asm_lit(RN_ADD); asm_host(HOST_EQ);
    cell j3 = asm_zbranch_fwd();
    pf_incr(PF_NAT_ADD);
    cell d3 = asm_branch_fwd();
    asm_patch_here(j3);
    e_cell(RV_NAT); asm_lit(RN_EITHER); asm_host(HOST_EQ);
    cell j4 = asm_zbranch_fwd();
    pf_incr(PF_NAT_EITHER);
    cell d4 = asm_branch_fwd();
    asm_patch_here(j4);
    pf_incr(PF_NAT_OTHER);
    asm_patch_here(d1);
    asm_patch_here(d2);
    asm_patch_here(d3);
    asm_patch_here(d4);
}
#endif

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
static cell to_invoke[8];      static int n_invoke;

/* ========================== M2 GC build ==================================
 * A non-moving, stop-the-world, exact mark/sweep collector + first-fit
 * allocator, emitted as S1 code (above the frozen substrate).  See
 * M2-GC-DESIGN.md for the object layout, root set and invariants.
 *
 * Simplification relied on (audited): the collected heap contains ONLY
 * closures, child contexts and frames.  Blocks, the global context and RAW
 * callables live in the loader heap and are permanent.  Loader BLOCKS never
 * contain a collected pointer (they hold parse-time values), so they are not
 * traced at all.  Loader CONTEXTS (the global context) may hold collected
 * pointers and are traced. */

static cell r_mark_value, r_mark_push, r_trace_ctx, r_trace_closure,
            r_mark_frame, r_trace_user, r_trace_string, r_trace_block, r_scan_values, r_scan_closures;

/* forward-call patch lists for the mark/trace mutual recursion */
static cell fw_mark_value[128]; static int n_fw_mv;
static cell fw_mark_frame[32];  static int n_fw_mf;

static cell call_mark_value(void) { cell c = emit_call_fwd(); fw_mark_value[n_fw_mv++] = c; return c; }
static cell call_mark_frame(void) { cell c = emit_call_fwd(); fw_mark_frame[n_fw_mf++] = c; return c; }

/* MARK-PUSH: ( p -- ) mark collected object p (payload ptr) if unmarked, push
 * it on the explicit mark worklist. */
static void emit_mark_push(void) {
    r_mark_push = asm_here();
    e_setc(GC_T1);                                   /* p */
    e_cell(GC_T1); asm_lit(GC_HDR_STRIDE - GC_HDR_FLAGS); asm_host(HOST_SUB); e_setc(GC_T2); /* flags addr */
    e_cell(GC_T2); asm_fetch(); e_setc(GC_T3);       /* flags */
    e_cell(GC_T3); asm_lit(2); asm_host(HOST_DIV); asm_lit(2); asm_host(HOST_MOD); /* mark bit */
    cell j_cont = asm_zbranch_fwd();                 /* not marked -> continue */
    asm_exit();                                      /* already marked */
    asm_patch_here(j_cont);
    e_cell(GC_T3); asm_lit(GC_FLAG_MARK); asm_host(HOST_ADD); e_cell(GC_T2); asm_store(); /* set mark */
    e_cell(GC_T1);                                    /* value p */
    e_cell(GC_WL_SP); asm_lit(GC_WORKLIST); asm_host(HOST_ADD);  /* address */
    asm_store();                                      /* worklist[sp] = p */
    e_cell(GC_WL_SP); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_WL_SP);
    asm_exit();
}

/* TRACE-CTX: ( p -- ) trace context parent + value cells. */
static void emit_trace_ctx(void) {
    r_trace_ctx = asm_here();
    e_setc(GC_T1);                                   /* p */
    e_cell(GC_T1); asm_toR();                        /* save p across parent mark */
    e_cell(GC_T1); asm_lit(CTX_PARENT); asm_host(HOST_ADD); asm_fetch();
    call_mark_value();                               /* parent */
    asm_fromR(); e_setc(GC_T1);                      /* restore p */
    e_cell(GC_T1); asm_lit(CTX_COUNT); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T4); /* count */
    asm_lit(0); e_setc(GC_T5);                       /* i = 0 */
    cell loop = asm_here();
    e_cell(GC_T5); e_cell(GC_T4); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    e_cell(GC_T1); asm_toR();                        /* save p */
    e_cell(GC_T4); asm_toR();                        /* save count */
    e_cell(GC_T5); asm_toR();                        /* save i */
    e_cell(GC_T1); asm_lit(CTX_DATA + 1); asm_host(HOST_ADD);
    e_cell(GC_T5); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD); asm_fetch();
    call_mark_value();                               /* value cell */
    asm_fromR(); e_setc(GC_T5);                      /* restore i */
    asm_fromR(); e_setc(GC_T4);                      /* restore count */
    asm_fromR(); e_setc(GC_T1);                      /* restore p */
    e_cell(GC_T5); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_T5);
    asm_branch(loop);
    asm_patch_here(j_done);
    asm_exit();
}

/* TRACE-CLOSURE: ( p -- ) trace spec/body/captured-context.
 * GC_T1 (the closure pointer) must be saved across each mark_value call: the
 * T_BLOCK/T_CONTEXT/T_USER/T_STRING cases in mark_value use GC_T1 as their own
 * scratch (p = v - tag), so an untraced spec/body block would clobber it. */
static void emit_trace_closure(void) {
    r_trace_closure = asm_here();
    e_setc(GC_T1);
    e_cell(GC_T1); asm_toR();                        /* save p */
    e_cell(GC_T1); asm_lit(CLOSURE_SPEC); asm_host(HOST_ADD); asm_fetch(); call_mark_value();
    asm_fromR(); e_setc(GC_T1);                      /* restore p */
    e_cell(GC_T1); asm_toR();                        /* save p */
    e_cell(GC_T1); asm_lit(CLOSURE_BODY); asm_host(HOST_ADD); asm_fetch(); call_mark_value();
    asm_fromR(); e_setc(GC_T1);                      /* restore p */
    e_cell(GC_T1); asm_lit(CLOSURE_CTX);  asm_host(HOST_ADD); asm_fetch(); call_mark_value();
    asm_exit();
}

/* TRACE-USER: ( p -- ) trace a generic user object. First verify COUNT against
 * the allocated block extent (a COUNT that exceeds the payload capacity is
 * fail-stop heap corruption, never silently clamped), then mark the descriptor
 * and each tagged content cell.
 *
 * trace_user is reached ONLY from the collector's drain loop, never from
 * mark_value (the mark_value T_USER case only mark_pushes). It may therefore
 * use the GC_T6..GC_T8 scan/drain partition for its locals: mark_value and
 * every routine it reaches (mark_push, trace_ctx, trace_closure, trace_frame)
 * use only GC_T1..GC_T5, so GC_T6..GC_T8 survive the recursive mark_value
 * calls and no >R/R> save/restore is needed. */
static void emit_trace_user(void) {
    r_trace_user = asm_here();
    e_setc(GC_T6);                                   /* p */
    /* count = M[p+1]; size = M[p-16]; require count <= size-18 (fail-stop) */
    e_cell(GC_T6); asm_lit(GC_HDR_STRIDE); asm_host(HOST_SUB); asm_fetch(); e_setc(GC_T5); /* size */
    e_cell(GC_T6); asm_lit(1); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T7);  /* count */
    e_cell(GC_T7);                                    /* count */
    e_cell(GC_T5); asm_lit(GC_HDR_STRIDE + 2); asm_host(HOST_SUB);              /* size-18 */
    asm_host(HOST_GT);                                /* count > size-18 ? */
    cell j_ok = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                  /* corrupt count */
    asm_patch_here(j_ok);
    /* mark descriptor (M[p]) */
    e_cell(GC_T6); asm_fetch();
    call_mark_value();
    /* mark each content cell M[p+2+i] */
    asm_lit(0); e_setc(GC_T8);                       /* i = 0 */
    cell loop = asm_here();
    e_cell(GC_T8); e_cell(GC_T7); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    e_cell(GC_T6); asm_lit(2); asm_host(HOST_ADD);
    e_cell(GC_T8); asm_host(HOST_ADD); asm_fetch();
    call_mark_value();                               /* content cell */
    e_cell(GC_T8); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_T8);
    asm_branch(loop);
    asm_patch_here(j_done);
    asm_exit();
}

/* TRACE-STRING: ( p -- ) trace a managed STRING (a leaf object). First verify
 * the byte length against the allocated block extent (a length that exceeds
 * the payload capacity is fail-stop heap corruption, never silently clamped),
 * then trace NO children -- the bytes are raw, non-pointer data.
 *
 * trace_string is reached ONLY from the collector's drain loop, so like
 * trace_user it may use the GC_T6..GC_T8 scan/drain partition for its locals. */
static void emit_trace_string(void) {
    r_trace_string = asm_here();
    e_setc(GC_T6);                                   /* p */
    /* length = M[p]; size = M[p-16]; require length <= size-17 (fail-stop) */
    e_cell(GC_T6); asm_lit(GC_HDR_STRIDE); asm_host(HOST_SUB); asm_fetch(); e_setc(GC_T5); /* size */
    e_cell(GC_T6); asm_fetch(); e_setc(GC_T7);        /* length */
    e_cell(GC_T7);                                    /* length */
    e_cell(GC_T5); asm_lit(GC_HDR_STRIDE + 1); asm_host(HOST_SUB);   /* size-17 */
    asm_host(HOST_GT);                                /* length > size-17 ? */
    cell j_ok = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                  /* corrupt length */
    asm_patch_here(j_ok);
    /* no children to trace */
    asm_exit();
}

/* TRACE-BLOCK: ( p -- ) trace a managed BLOCK (a tagged-value series). Managed
 * blocks share the loader-block layout [count, site_id, elem...] so the
 * block-at/bset/block-len primitives operate on them unchanged; the site_id
 * slot (always 0 at runtime) is never traced. First verify length against the
 * allocated block extent (fail-stop, never clamp), then mark_value each tagged
 * element. The alignment padding is never traced.
 *
 * trace_block is reached ONLY from the collector's drain loop, so like
 * trace_user/trace_string it may use the GC_T6..GC_T8 scan/drain partition for
 * its locals; mark_value (GC_T1..GC_T5) is called per element and the loop
 * state (GC_T6/GC_T7/GC_T8) survives those calls. */
static void emit_trace_block(void) {
    r_trace_block = asm_here();
    e_setc(GC_T6);                                   /* p */
    /* length = M[p]; size = M[p-16]; require length <= size-18 (fail-stop) */
    e_cell(GC_T6); asm_lit(GC_HDR_STRIDE); asm_host(HOST_SUB); asm_fetch(); e_setc(GC_T5); /* size */
    e_cell(GC_T6); asm_fetch(); e_setc(GC_T7);        /* length */
    e_cell(GC_T7);                                    /* length */
    e_cell(GC_T5); asm_lit(GC_HDR_STRIDE + 2); asm_host(HOST_SUB);   /* size-18 */
    asm_host(HOST_GT);                                /* length > size-18 ? */
    cell j_ok = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                  /* corrupt length */
    asm_patch_here(j_ok);
    /* mark each element M[p+BLK_DATA+i] (skip the site_id slot) */
    asm_lit(0); e_setc(GC_T8);                        /* i = 0 */
    cell loop = asm_here();
    e_cell(GC_T8); e_cell(GC_T7); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    e_cell(GC_T6); asm_lit(BLK_DATA); asm_host(HOST_ADD);
    e_cell(GC_T8); asm_host(HOST_ADD); asm_fetch();
    call_mark_value();                                /* element */
    e_cell(GC_T8); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_T8);
    asm_branch(loop);
    asm_patch_here(j_done);
    asm_exit();
}

/* MARK-FRAME: ( p -- ) trace a live activation frame as GC ROOT STORAGE.
 *
 * Since FIB-OPT-P2 frames are no longer managed heap objects (they live on the
 * task-local return stack), the collector must NOT mark the frame record
 * itself. It must instead trace the frame's pointer-bearing contents -- its
 * `prev` link (the previous frame, recursively) and its `CTX` field (the saved
 * caller context, a tagged managed value). BLK is a permanent loader block and
 * CUR/END are raw loader-block addresses, so they are never traced.
 *
 * This is the old trace_frame logic plus the old mark_frame's zero check; the
 * frame chain is walked directly instead of going through the mark worklist. */
static void emit_mark_frame(void) {
    r_mark_frame = asm_here();
    e_setc(GC_T1);                                   /* p */
    e_cell(GC_T1); asm_lit(0); asm_host(HOST_EQ);
    cell j_zero = asm_zbranch_fwd();
    asm_exit();                                      /* p == 0 */
    asm_patch_here(j_zero);
    e_cell(GC_T1); asm_toR();                        /* save p across prev mark */
    e_cell(GC_T1); asm_lit(FRAME_PREV); asm_host(HOST_ADD); asm_fetch(); call_mark_frame();
    asm_fromR(); e_setc(GC_T1);                      /* restore p */
    e_cell(GC_T1); asm_lit(FRAME_CTX);  asm_host(HOST_ADD); asm_fetch(); call_mark_value();
    asm_exit();
}

/* mark a collected closure pointer (or skip if not in the collected heap);
 * emitted inline at the RV_CLOSURE root and in the return-stack scan. The
 * pointer must be 16-aligned AND carry a GC_KIND_CLOSURE header: since
 * FIB-OPT-P2/P3 the return stack carries activation frames (whose saved-CTX
 * field is a TAGGED context, p+7) and stack-local contexts (whose value cells
 * are tagged, but whose integer values are mk_int = n*16, 16-aligned), so the
 * conservative scan must skip both rather than misread them as closures. */
static void emit_mark_closure_inline(void) {
    e_setc(GC_T1);                                   /* p */
    e_cell(GC_T1); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j1 = asm_zbranch_fwd();
    e_cell(GC_T1); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j2 = asm_zbranch_fwd();
    e_cell(GC_T1); asm_lit(16); asm_host(HOST_MOD); asm_lit(0); asm_host(HOST_EQ);
    cell j3 = asm_zbranch_fwd();                     /* misaligned -> skip */
    e_cell(GC_T1); asm_lit(GC_HDR_STRIDE - GC_HDR_FLAGS); asm_host(HOST_SUB); asm_fetch(); /* flags */
    asm_lit(4); asm_host(HOST_DIV); asm_lit(8); asm_host(HOST_MOD);                     /* kind */
    asm_lit(GC_KIND_CLOSURE); asm_host(HOST_EQ);
    cell j4 = asm_zbranch_fwd();                     /* not a closure -> skip */
    e_cell(GC_T1); asm_call(r_mark_push);
    asm_patch_here(j4);
    asm_patch_here(j3);
    asm_patch_here(j2);
    asm_patch_here(j1);
}

/* MARK-VALUE: ( v -- ) dispatch on the R0 tag. */
static void emit_mark_value(void) {
    r_mark_value = asm_here();
    asm_dup(); asm_lit(16); asm_host(HOST_MOD); e_setc(GC_T2); /* tag (v left on stack) */
    /* CONTEXT (tag 7): classification. A managed context (payload in the managed
     * heap) is mark_pushed; a task-local stack context (main return stack
     * [16384,24576) or an M1 task's arena) is traced as root
     * storage, never mark_pushed; a permanent loader context (global) is traced.
     * Anything else is fail-stop corruption. */
    e_cell(GC_T2); asm_lit(T_CONTEXT); asm_host(HOST_EQ);
    cell j_nc = asm_zbranch_fwd();
    asm_lit(T_CONTEXT); asm_host(HOST_SUB); e_setc(GC_T1);   /* p */
    /* managed [32768,40000) */
    e_cell(GC_T1); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j_not_mg = asm_zbranch_fwd();
    e_cell(GC_T1); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_not_mg2 = asm_zbranch_fwd();
    e_cell(GC_T1); asm_call(r_mark_push); asm_exit();        /* collected ctx */
    asm_patch_here(j_not_mg2);
    asm_patch_here(j_not_mg);
    /* main stack [16384,24576) */
    e_cell(GC_T1); asm_lit(R0S1_DS_INIT); asm_host(HOST_GE);
    cell j_not_stk = asm_zbranch_fwd();
    e_cell(GC_T1); asm_lit(R0S1_RS_INIT); asm_host(HOST_LT);
    cell j_not_stk2 = asm_zbranch_fwd();
    e_cell(GC_T1); asm_call(r_trace_ctx); asm_exit();        /* stack ctx (root) */
    asm_patch_here(j_not_stk2);
    asm_patch_here(j_not_stk);
    /* M1 task arena */
    e_cell(GC_T1); asm_lit(M1_ARENA_BASE); asm_host(HOST_GE);
    cell j_not_m1 = asm_zbranch_fwd();
    e_cell(GC_T1); asm_lit(M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS); asm_host(HOST_LT);
    cell j_not_m12 = asm_zbranch_fwd();
    e_cell(GC_T1); asm_call(r_trace_ctx); asm_exit();        /* M1 stack ctx (root) */
    asm_patch_here(j_not_m12);
    asm_patch_here(j_not_m1);
    /* loader [R0S1_HEAP_BASE, R0S1_HEAP_LIMIT) */
    e_cell(GC_T1); asm_lit(R0S1_HEAP_BASE); asm_host(HOST_GE);
    cell j_ldr1 = asm_zbranch_fwd();
    e_cell(GC_T1); asm_lit(R0S1_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_ldr2 = asm_zbranch_fwd();
    e_cell(GC_T1); asm_call(r_trace_ctx); asm_exit();        /* loader ctx (global) */
    asm_patch_here(j_ldr2);
    asm_patch_here(j_ldr1);
    asm_host(HOST_DUMP); asm_halt();                        /* out-of-range T_CONTEXT */
    asm_patch_here(j_nc);
    /* CLOSURE (tag 8) */
    e_cell(GC_T2); asm_lit(T_CLOSURE); asm_host(HOST_EQ);
    cell j_ncl = asm_zbranch_fwd();
    asm_lit(T_CLOSURE); asm_host(HOST_SUB);                  /* p = v-8 */
    emit_mark_closure_inline();                              /* collected -> mark; else skip */
    asm_exit();
    asm_patch_here(j_ncl);
    /* USER (tag 11): mark_push the collected user object, but first require the
     * payload pointer to be inside the managed heap. There are no loader user
     * objects (values and descriptors are always managed, allocated via r_alloc
     * or seeded at the bottom of the managed heap), so an out-of-range payload
     * is fail-stop heap corruption, not something to trace. */
    e_cell(GC_T2); asm_lit(T_USER); asm_host(HOST_EQ);
    cell j_nu = asm_zbranch_fwd();
    cell l_user = asm_here();                               /* ERROR joins here */
    asm_lit(T_USER); asm_host(HOST_SUB); e_setc(GC_T1);     /* p */
    e_cell(GC_T1); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j_bad1 = asm_zbranch_fwd();                        /* p < base -> corrupt */
    e_cell(GC_T1); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_bad2 = asm_zbranch_fwd();                        /* p >= limit -> corrupt */
    e_cell(GC_T1); asm_call(r_mark_push); asm_exit();       /* collected user obj */
    asm_patch_here(j_bad2);
    asm_patch_here(j_bad1);
    asm_host(HOST_DUMP); asm_halt();                        /* out-of-range T_USER */
    asm_patch_here(j_nu);
    /* STRING (tag 12): like USER, mark_push the managed string, but first
     * require the payload pointer to be inside the managed heap. Strings are
     * leaf objects (no pointer-bearing fields), so mark_value only mark_pushes;
     * trace_string (from the drain loop) validates length and traces nothing. */
    e_cell(GC_T2); asm_lit(T_STRING); asm_host(HOST_EQ);
    cell j_ns = asm_zbranch_fwd();
    asm_lit(T_STRING); asm_host(HOST_SUB); e_setc(GC_T1);   /* p */
    e_cell(GC_T1); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j_sbad1 = asm_zbranch_fwd();                       /* p < base -> corrupt */
    e_cell(GC_T1); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_sbad2 = asm_zbranch_fwd();                       /* p >= limit -> corrupt */
    e_cell(GC_T1); asm_call(r_mark_push); asm_exit();       /* collected string */
    asm_patch_here(j_sbad2);
    asm_patch_here(j_sbad1);
    asm_host(HOST_DUMP); asm_halt();                        /* out-of-range T_STRING */
    asm_patch_here(j_ns);
    /* ERROR (tag 14): a SIN! payload is a managed GC_KIND_USER object.
     * Rebase the value to USER's tag and share USER's range check and
     * mark_push; the drain loop's trace_user then marks type/id/arg. */
    e_cell(GC_T2); asm_lit(T_ERROR); asm_host(HOST_EQ);
    cell j_ne = asm_zbranch_fwd();
    asm_lit(T_ERROR - T_USER); asm_host(HOST_SUB);
    asm_branch(l_user);
    asm_patch_here(j_ne);
    /* BLOCK (tag 6): three-way classification. A managed block (payload in the
     * managed heap) must be an allocated GC_KIND_BLOCK payload start; a
     * permanent loader block (payload in the loader heap) has no children;
     * anything else (below heap, interior, unallocated, wrong kind, above the
     * loader heap) is fail-stop corruption. */
    e_cell(GC_T2); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_nb = asm_zbranch_fwd();                        /* not BLOCK -> skip */
    asm_lit(T_BLOCK); asm_host(HOST_SUB); e_setc(GC_T1);   /* p = v - T_BLOCK */
    /* G1 permanent emit buffer (G1_OUT): a loader-style byte-list block below
     * the managed heap whose cells are all tagged ints (rendered bytes), so it
     * is an opaque leaf with no collected children. Exact, not a range check. */
    e_cell(GC_T1); asm_lit(G1_OUT); asm_host(HOST_EQ);
    cell j_g1out = asm_zbranch_fwd();
    asm_exit();                                          /* permanent emit buffer */
    asm_patch_here(j_g1out);
    /* p < GC_HEAP_BASE -> corrupt */
    e_cell(GC_T1); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j_below = asm_zbranch_fwd();
    /* p < GC_HEAP_LIMIT -> managed range */
    e_cell(GC_T1); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_not_managed = asm_zbranch_fwd();
    /* validate header h = p - 16: allocated GC_KIND_BLOCK payload start */
    e_cell(GC_T1); asm_lit(GC_HDR_STRIDE); asm_host(HOST_SUB); e_setc(GC_T3); /* h */
    e_cell(GC_T3); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j_h = asm_zbranch_fwd();                          /* h < base -> corrupt */
    e_cell(GC_T3); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T4); /* flags */
    e_cell(GC_T4); asm_lit(2); asm_host(HOST_MOD);          /* alloc bit */
    asm_lit(1); asm_host(HOST_EQ);
    cell j_a = asm_zbranch_fwd();                          /* unallocated -> corrupt */
    e_cell(GC_T4); asm_lit(4); asm_host(HOST_DIV); asm_lit(8); asm_host(HOST_MOD); /* kind */
    asm_lit(GC_KIND_BLOCK); asm_host(HOST_EQ);
    cell j_k = asm_zbranch_fwd();                          /* wrong kind -> corrupt */
    e_cell(GC_T1); asm_call(r_mark_push); asm_exit();      /* valid managed block */
    asm_patch_here(j_k);
    asm_patch_here(j_a);
    asm_patch_here(j_h);
    asm_host(HOST_DUMP); asm_halt();                        /* corrupt managed pointer */
    /* p >= GC_HEAP_LIMIT: permanent loader block? */
    asm_patch_here(j_not_managed);
    e_cell(GC_T1); asm_lit(R0S1_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_not_loader = asm_zbranch_fwd();                  /* p >= R0S1_HEAP_LIMIT -> corrupt */
    asm_exit();                                             /* loader block: no children */
    asm_patch_here(j_not_loader);
    asm_patch_here(j_below);
    asm_host(HOST_DUMP); asm_halt();                        /* out-of-range T_BLOCK */
    asm_patch_here(j_nb);
    /* else: RAW/int/none/word/set/get/lit/native -> no collected children */
    asm_drop();
    asm_exit();
}

/* SCAN-VALUES: ( lo hi -- ) scan [lo,hi) as tagged values. */
static void emit_scan_values(void) {
    r_scan_values = asm_here();
    e_setc(GC_T7);                                   /* hi */
    e_setc(GC_T6);                                   /* lo */
    cell loop = asm_here();
    e_cell(GC_T6); e_cell(GC_T7); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    e_cell(GC_T6); asm_fetch(); asm_call(r_mark_value);
    e_cell(GC_T6); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_T6);
    asm_branch(loop);
    asm_patch_here(j_done);
    asm_exit();
}

/* SCAN-CLOSURES: ( lo hi -- ) scan [lo,hi); cells in the collected heap are
 * closure pointers (the exact structural invariant of the return stack). */
static void emit_scan_closures(void) {
    r_scan_closures = asm_here();
    e_setc(GC_T7);                                   /* hi */
    e_setc(GC_T6);                                   /* lo */
    cell loop = asm_here();
    e_cell(GC_T6); e_cell(GC_T7); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    e_cell(GC_T6); asm_fetch();                       /* c */
    emit_mark_closure_inline();
    e_cell(GC_T6); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_T6);
    asm_branch(loop);
    asm_patch_here(j_done);
    asm_exit();
}

/* COALESCE-FREE was inlined into the sweep (see emit_collect). */

/* COLLECT: ( -- ) global stop-the-world mark/sweep. */
static void emit_collect(void) {
    r_collect = asm_here();
    /* capture SP/RP before any temporary pushes */
    asm_lit(REG_SP); asm_fetch(); e_setc(GC_C1);     /* SP0 */
    asm_lit(REG_RP); asm_fetch(); e_setc(GC_C2);     /* RP0 */
    e_cell(GC_COLLECT_CNT); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_COLLECT_CNT);
    asm_lit(0); e_setc(GC_LIVE_CELLS);
    asm_lit(0); e_setc(GC_LIVE_OBJS);
    asm_lit(0); e_setc(GC_FREE_BLOCKS);
    asm_lit(0); e_setc(GC_FREE_CELLS);
    asm_lit(0); e_setc(GC_WL_SP);

    /* roots: global context + active evaluator state */
    e_cell(GC_GLOBAL_CTX); asm_call(r_mark_value);
    e_cell(RV_CTX);  asm_call(r_mark_value);
    e_cell(RV_CHILD); asm_call(r_mark_value);
    e_cell(RV_CLOSURE); emit_mark_closure_inline();
    e_cell(RV_FRAME); asm_call(r_mark_frame);

    /* bootstrap datatype descriptors (permanent roots, independent of word
     * bindings). The fixed 16-slot BUILTIN_TYPE table is scanned every
     * collection; the datatype! meta-descriptor is transitively rooted through
     * every descriptor's desc field (each built-in descriptor in the table
     * points at it), so no separate meta root is needed. */
    asm_lit(BUILTIN_BASE); asm_lit(BUILTIN_BASE + 16); asm_call(r_scan_values);

    /* active world DS/RS. Classify SP0 into a VALID region rather than a
     * heuristic: SP0 <= DS_INIT => main world; SP0 inside a task's DS region
     * => that task (+ its scheduler world); anything else is fail-stop
     * corruption. This guarantees the task slot is never negative and never
     * derived from arbitrary SP arithmetic. */
    e_cell(GC_C1); asm_lit(R0S1_DS_INIT); asm_host(HOST_GT);   /* SP0 > DS_INIT -> task? */
    cell j_main_world = asm_zbranch_fwd();                      /* else (SP0 <= DS_INIT) -> main */
    /* task? SP0 in [M1_ARENA_BASE, M1_ARENA_BASE + M1_MAX_TASKS*M1_TASK_CELLS) */
    e_cell(GC_C1); asm_lit(M1_ARENA_BASE); asm_host(HOST_GE);
    cell j_bad_sp = asm_zbranch_fwd();                          /* below arena -> corrupt */
    e_cell(GC_C1); asm_lit(M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS); asm_host(HOST_LT);
    cell j_bad_sp2 = asm_zbranch_fwd();                         /* at/above arena end -> corrupt */
    /* slot = (SP0 - base) / cells  (0 <= slot < M1_MAX_TASKS by the range above) */
    e_cell(GC_C1); asm_lit(M1_ARENA_BASE); asm_host(HOST_SUB);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_DIV); e_setc(GC_C3);  /* slot */
    e_cell(GC_C3); asm_lit(M1_TASK_CELLS); asm_host(HOST_MUL);
    asm_lit(M1_ARENA_BASE); asm_host(HOST_ADD); e_setc(GC_C4);  /* arena base */
    /* SP0 must lie inside the task's DS region [base, base+M1_DS_OFF] */
    e_cell(GC_C1); e_cell(GC_C4); asm_lit(M1_DS_OFF); asm_host(HOST_ADD); asm_host(HOST_LE);
    cell j_bad_sp3 = asm_zbranch_fwd();                         /* in RS/out of DS region -> corrupt */
    /* a task is running: its DS/RS + scheduler world */
    e_cell(GC_C1); e_cell(GC_C4); asm_lit(M1_DS_OFF); asm_host(HOST_ADD); asm_call(r_scan_values);
    e_cell(GC_C2); e_cell(GC_C4); asm_lit(M1_RS_OFF); asm_host(HOST_ADD); asm_call(r_scan_closures);
    e_cell(M1_SCHED_REC + 1); asm_lit(R0S1_DS_INIT); asm_call(r_scan_values);
    e_cell(M1_SCHED_REC + 2); asm_lit(R0S1_RS_INIT); asm_call(r_scan_closures);
    cell fw_active_done = asm_branch_fwd();
    asm_patch_here(j_main_world);
    /* main world: standard DS/RS */
    e_cell(GC_C1); asm_lit(R0S1_DS_INIT); asm_call(r_scan_values);
    e_cell(GC_C2); asm_lit(R0S1_RS_INIT); asm_call(r_scan_closures);
    cell fw_main_done = asm_branch_fwd();
    asm_patch_here(j_bad_sp3);
    asm_patch_here(j_bad_sp2);
    asm_patch_here(j_bad_sp);
    asm_host(HOST_DUMP); asm_halt();                            /* invalid/corrupt SP */
    asm_patch_here(fw_active_done);
    asm_patch_here(fw_main_done);

    /* every task record (saved tasks are roots) */
    asm_lit(0); e_setc(GC_C3);
    cell tloop = asm_here();
    e_cell(GC_C3); asm_lit(M1_MAX_TASKS); asm_host(HOST_LT);
    cell j_tdone = asm_zbranch_fwd();
    e_cell(GC_C3); asm_lit(M1_TASK_REC_SIZE); asm_host(HOST_MUL);
    asm_lit(M1_TASK_TABLE); asm_host(HOST_ADD); e_setc(GC_C4);       /* rec */
    e_cell(GC_C4); asm_lit(TREC_STATE); asm_host(HOST_ADD); asm_fetch();
    asm_lit(TASK_RUNNABLE); asm_host(HOST_EQ);
    cell j_skip = asm_zbranch_fwd();
    e_cell(GC_C4); asm_lit(TREC_CTX); asm_host(HOST_ADD); asm_fetch(); asm_call(r_mark_value);
    e_cell(GC_C4); asm_lit(TREC_FRAME); asm_host(HOST_ADD); asm_fetch(); asm_call(r_mark_frame);
    e_cell(GC_C3); asm_lit(M1_TASK_CELLS); asm_host(HOST_MUL);
    asm_lit(M1_ARENA_BASE); asm_host(HOST_ADD); e_setc(GC_C5);       /* top base */
    /* task stack sentry: a RUNNABLE task's saved SP/RP must lie inside its
     * arena DS [base, base+DS_OFF] and RS [base+DS_OFF, base+RS_OFF]; fail-stop
     * (not corrupt) if a saved register is out of bounds. */
    e_cell(GC_C4); asm_lit(TREC_SP); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T7); /* SP */
    e_cell(GC_T7); e_cell(GC_C5); asm_host(HOST_GE);
    cell j_tbad = asm_zbranch_fwd();                     /* SP < base */
    e_cell(GC_T7); e_cell(GC_C5); asm_lit(M1_DS_OFF); asm_host(HOST_ADD); asm_host(HOST_LE);
    cell j_tbad2 = asm_zbranch_fwd();                    /* SP > base+DS_OFF */
    e_cell(GC_C4); asm_lit(TREC_RP); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T8); /* RP */
    e_cell(GC_T8); e_cell(GC_C5); asm_lit(M1_DS_OFF); asm_host(HOST_ADD); asm_host(HOST_GE);
    cell j_tbad3 = asm_zbranch_fwd();                    /* RP < base+DS_OFF */
    e_cell(GC_T8); e_cell(GC_C5); asm_lit(M1_RS_OFF); asm_host(HOST_ADD); asm_host(HOST_LE);
    cell j_tbad4 = asm_zbranch_fwd();                    /* RP > base+RS_OFF */
    cell fw_tok = asm_branch_fwd();
    asm_patch_here(j_tbad4);
    asm_patch_here(j_tbad3);
    asm_patch_here(j_tbad2);
    asm_patch_here(j_tbad);
    asm_host(HOST_DUMP); asm_halt();                     /* task stack bounds violated */
    asm_patch_here(fw_tok);
    e_cell(GC_C4); asm_lit(TREC_SP); asm_host(HOST_ADD); asm_fetch();
    e_cell(GC_C5); asm_lit(M1_DS_OFF); asm_host(HOST_ADD); asm_call(r_scan_values);
    e_cell(GC_C4); asm_lit(TREC_RP); asm_host(HOST_ADD); asm_fetch();
    e_cell(GC_C5); asm_lit(M1_RS_OFF); asm_host(HOST_ADD); asm_call(r_scan_closures);
    asm_patch_here(j_skip);
    e_cell(GC_C3); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_C3);
    asm_branch(tloop);
    asm_patch_here(j_tdone);

    /* drain the mark worklist */
    cell drain = asm_here();
    e_cell(GC_WL_SP); asm_lit(0); asm_host(HOST_GT);
    cell j_drain_done = asm_zbranch_fwd();
    e_cell(GC_WL_SP); asm_lit(1); asm_host(HOST_SUB); e_setc(GC_WL_SP);
    e_cell(GC_WL_SP); asm_lit(GC_WORKLIST); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T6); /* p */
    e_cell(GC_T6); asm_lit(GC_HDR_STRIDE - GC_HDR_FLAGS); asm_host(HOST_SUB); asm_fetch(); e_setc(GC_T7); /* flags */
    e_cell(GC_T7); asm_lit(4); asm_host(HOST_DIV); asm_lit(8); asm_host(HOST_MOD); e_setc(GC_T8); /* kind */
    e_cell(GC_T8); asm_lit(GC_KIND_CTX); asm_host(HOST_EQ);
    cell j_nctx = asm_zbranch_fwd();
    e_cell(GC_T6); asm_call(r_trace_ctx); asm_branch(drain);
    asm_patch_here(j_nctx);
    e_cell(GC_T8); asm_lit(GC_KIND_CLOSURE); asm_host(HOST_EQ);
    cell j_ncl = asm_zbranch_fwd();
    e_cell(GC_T6); asm_call(r_trace_closure); asm_branch(drain);
    asm_patch_here(j_ncl);
    e_cell(GC_T8); asm_lit(GC_KIND_USER); asm_host(HOST_EQ);
    cell j_nusr = asm_zbranch_fwd();
    e_cell(GC_T6); asm_call(r_trace_user); asm_branch(drain);
    asm_patch_here(j_nusr);
    e_cell(GC_T8); asm_lit(GC_KIND_STRING); asm_host(HOST_EQ);
    cell j_nstr = asm_zbranch_fwd();
    e_cell(GC_T6); asm_call(r_trace_string); asm_branch(drain);
    asm_patch_here(j_nstr);
    e_cell(GC_T8); asm_lit(GC_KIND_BLOCK); asm_host(HOST_EQ);
    cell j_nblk = asm_zbranch_fwd();
    e_cell(GC_T6); asm_call(r_trace_block); asm_branch(drain);
    asm_patch_here(j_nblk);
    asm_branch(drain);
    asm_patch_here(j_drain_done);

    /* sweep [GC_HEAP_BASE, REG_HP) */
    asm_lit(GC_HEAP_BASE); e_setc(GC_T6);            /* addr */
    asm_lit(0); e_setc(GC_C6);                       /* prev_free */
    cell sloop = asm_here();
    e_cell(GC_T6); asm_lit(REG_HP); asm_fetch(); asm_host(HOST_LT);
    cell j_sdone = asm_zbranch_fwd();
    e_cell(GC_T6); asm_fetch(); e_setc(GC_T7);       /* size */
    e_cell(GC_T6); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T8); /* flags */
    e_cell(GC_T8); asm_lit(2); asm_host(HOST_MOD);   /* alloc bit */
    cell j_free = asm_zbranch_fwd();                 /* 0 -> free */
    /* allocated: mark bit */
    e_cell(GC_T8); asm_lit(2); asm_host(HOST_DIV); asm_lit(2); asm_host(HOST_MOD);
    cell j_dead = asm_zbranch_fwd();                 /* 0 -> dead */
    /* live: clear mark, count */
    e_cell(GC_T8); asm_lit(GC_FLAG_MARK); asm_host(HOST_SUB);
    e_cell(GC_T6); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_store();
    e_cell(GC_LIVE_CELLS); e_cell(GC_T7); asm_host(HOST_ADD); e_setc(GC_LIVE_CELLS);
    e_cell(GC_LIVE_OBJS); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_LIVE_OBJS);
    asm_lit(0); e_setc(GC_C6);
    cell fw_live_next = asm_branch_fwd();
    asm_patch_here(j_dead);
    /* dead: free it (fall through to shared free/coalesce) */
    asm_lit(0); e_cell(GC_T6); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_store();
    asm_patch_here(j_free);
    /* free (or dead): count + coalesce with previous run */
    e_cell(GC_FREE_CELLS); e_cell(GC_T7); asm_host(HOST_ADD); e_setc(GC_FREE_CELLS);
    e_cell(GC_C6); asm_lit(0); asm_host(HOST_NE);   /* prev_free != 0 -> coalesce */
    cell j_new = asm_zbranch_fwd();
    e_cell(GC_C6); asm_fetch(); e_cell(GC_T7); asm_host(HOST_ADD); e_cell(GC_C6); asm_store();
    cell fw_coal_next = asm_branch_fwd();
    asm_patch_here(j_new);
    e_cell(GC_T6); e_setc(GC_C6);                    /* prev_free = addr */
    e_cell(GC_FREE_BLOCKS); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_FREE_BLOCKS);
    asm_patch_here(fw_live_next);
    asm_patch_here(fw_coal_next);
    e_cell(GC_T6); e_cell(GC_T7); asm_host(HOST_ADD); e_setc(GC_T6);
    asm_branch(sloop);
    asm_patch_here(j_sdone);

    e_cell(GC_FREE_CELLS); e_setc(GC_LAST_RECLAM);
    asm_exit();
}

/* ALLOC: ( n kind -- payload-addr ) first-fit + bump + collect.
 *
 * GC-safepoint invariant (runtime law): allocation may trigger a collection,
 * and the collector scans the Glon data stack, return stack, and roots assuming
 * every cell is a valid tagged Glon value.  Therefore at every allocation the
 * DS must contain only tagged Glon values -- never raw implementation values
 * (untagged integers, pointer ids, site-ids, bias, counts).  Audit: the closure
 * allocation was the exposed violation (raw site-id/bias left on the DS; fixed
 * in emit_mkclosure by popping them first); the context-promotion allocation
 * was already safe (only tagged spec/body on the DS); alloc's own (n kind)
 * arguments are popped before the collector runs; no other runtime-generated
 * raw-DS-across-GC hazard remains. */
static void emit_alloc(void) {
    r_alloc = asm_here();
    e_setc(GC_A2);                                   /* kind */
    e_setc(GC_T7);                                   /* n */
    e_cell(GC_T7); asm_lit(GC_HDR_STRIDE); asm_host(HOST_ADD); e_setc(GC_A1); /* extent */
#ifdef R0_S1_PROFILE
    pf_incr(PF_ALLOCS);                              /* one allocation call */
    pf_incr_by(PF_ALLOC_CELLS, GC_A1);               /* cells = header+payload */
#endif
    /* tooling-heap escape: if REG_HP >= GC_HEAP_LIMIT, plain bump (historical tooling) */
    asm_lit(REG_HP); asm_fetch(); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_GT);
    cell j_tool = asm_zbranch_fwd();
    asm_lit(REG_HP); asm_fetch();                    /* a */
    asm_dup();                                       /* a a */
    e_cell(GC_T7); asm_host(HOST_ADD); asm_lit(REG_HP); asm_store(); /* HP = a+n; a */
    asm_exit();
    asm_patch_here(j_tool);

    asm_lit(0); e_setc(GC_T6);                       /* attempt = 0 */
    cell rtry = asm_here();
    asm_lit(GC_HEAP_BASE); e_setc(GC_T2);            /* addr */
    cell rscan = asm_here();
    e_cell(GC_T2); asm_lit(REG_HP); asm_fetch(); asm_host(HOST_LT);
    cell j_noscan = asm_zbranch_fwd();
    e_cell(GC_T2); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T3);
    e_cell(GC_T2); asm_fetch(); e_setc(GC_T4);       /* size */
    e_cell(GC_T3); asm_lit(0); asm_host(HOST_EQ);    /* free? */
    cell j_nf = asm_zbranch_fwd();
    e_cell(GC_T4); e_cell(GC_A1); asm_host(HOST_GE); /* size >= extent? */
    cell j_nb = asm_zbranch_fwd();
    cell fw_found = asm_branch_fwd();
    asm_patch_here(j_nb);
    asm_patch_here(j_nf);
    e_cell(GC_T2); e_cell(GC_T4); asm_host(HOST_ADD); e_setc(GC_T2);
    asm_branch(rscan);
    asm_patch_here(j_noscan);
    /* bump */
    asm_lit(REG_HP); asm_fetch(); e_cell(GC_A1); asm_host(HOST_ADD);
    asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LE);
    cell j_nobump = asm_zbranch_fwd();
    asm_lit(REG_HP); asm_fetch(); e_setc(GC_T2);     /* addr = HP */
    asm_lit(REG_HP); asm_fetch(); e_cell(GC_A1); asm_host(HOST_ADD); asm_lit(REG_HP); asm_store();
    e_cell(GC_A1); e_cell(GC_T2); asm_store();       /* header size */
    e_cell(GC_A2); asm_lit(4); asm_host(HOST_MUL); asm_lit(GC_FLAG_ALLOC); asm_host(HOST_ADD);
    e_cell(GC_T2); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_store(); /* flags */
    e_cell(GC_T2); asm_lit(GC_HDR_STRIDE); asm_host(HOST_ADD);
    asm_exit();
    asm_patch_here(j_nobump);
    /* collect once, then retry */
    e_cell(GC_T6); asm_lit(0); asm_host(HOST_EQ);
    cell j_oom = asm_zbranch_fwd();                  /* attempt != 0 -> OOM */
    asm_call(r_collect);
    asm_lit(1); e_setc(GC_T6);
    asm_branch(rtry);
    asm_patch_here(j_oom);
    /* genuine out-of-memory */
    asm_host(HOST_DUMP); asm_halt();

    asm_patch_here(fw_found);
    /* split if remainder >= 32 */
    e_cell(GC_T4); e_cell(GC_A1); asm_host(HOST_SUB); e_setc(GC_T5); /* remainder */
    e_cell(GC_T5); asm_lit(2 * GC_HDR_STRIDE); asm_host(HOST_GE);
    cell j_nosplit = asm_zbranch_fwd();
    e_cell(GC_A1); e_cell(GC_T2); asm_store();       /* shrink */
    e_cell(GC_T5);
    e_cell(GC_T2); e_cell(GC_A1); asm_host(HOST_ADD); asm_store(); /* remainder header size */
    asm_lit(0);
    e_cell(GC_T2); e_cell(GC_A1); asm_host(HOST_ADD); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_store();
    asm_patch_here(j_nosplit);
    /* commit: flags = ALLOC | kind<<2 */
    e_cell(GC_A2); asm_lit(4); asm_host(HOST_MUL); asm_lit(GC_FLAG_ALLOC); asm_host(HOST_ADD);
    e_cell(GC_T2); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_store();
    e_cell(GC_T2); asm_lit(GC_HDR_STRIDE); asm_host(HOST_ADD);
    asm_exit();
}

/* ================================ SIN! =====================================
 * A SIN! is a first-class value (tag T_ERROR) whose payload is a small
 * managed GC_KIND_USER object [desc = none, count = 3, type, id, arg], so the
 * collector traces it generically (trace_user). Possessing, storing, passing
 * or comparing a SIN! never raises: it is inert data.
 *
 * Propagation is control flow, kept separate from the value. RAISE (or a
 * converted runtime check) leaves the error in the RV_ERR* registers and
 * branches to RAISE-UNWIND, which walks the activation-frame chain for the
 * nearest JUDGE frame and restores it exactly as RETURN restores its target
 * (SP, RP, CTX, CUR, END, BLK, FRAME; RV_CHILD/RV_CLOSURE cleared). The judge
 * landing pad then builds the SIN! value -- in a clean evaluator state, so
 * that allocation is an ordinary GC safepoint -- and returns it as JUDGE's
 * result. No judge: the run halts exactly like the old fail-stop, with
 * RV_ERRUNC set so the host can report the error.
 *
 * Judge frames are ordinary links in the per-world RV_FRAME chain, so a task
 * only ever unwinds to its own judges (M1 task records already save and restore
 * RV_FRAME), and nothing else needs a new stack or fixed memory region. */

static cell r_mkerror, r_raise_unwind, r_trap_landing, r_raise3, r_raise_escape;

/* Word literals emitted before r0_s1_init preloads the symbol table (which
 * must allocate func/return/raw as symbols 0/1/2 first): each is emitted as a
 * placeholder LIT and patched with the interned word after the preload. */
typedef struct { cell patch; const char *name; } word_ref_t;
static word_ref_t word_refs[32];
static int n_word_refs;
static void emit_word_lit(const char *name) {
    if (n_word_refs >= (int)(sizeof word_refs / sizeof word_refs[0])) {
        fprintf(stderr, "r0_s1: word_refs table full\n");
        abort();
    }
    word_refs[n_word_refs].patch = asm_lit_fwd();
    word_refs[n_word_refs].name = name;
    n_word_refs++;
}

/* Raise a runtime error SIN! [type, id, the value in register arg_cell]:
 * push the three fields and branch to RAISE3 (never returns). */
static void emit_raise_fields(const char *type, const char *id, cell arg_cell) {
    emit_word_lit(type); emit_word_lit(id); e_cell(arg_cell);
    asm_branch(r_raise3);
}

/* MKERROR: ( type id arg -- err ) allocate a SIN!. The three values stay on
 * the data stack across the allocation, so a collection it triggers sees them
 * as roots. */
static void emit_mkerror(void) {
    r_mkerror = asm_here();
    asm_lit(16); asm_lit(GC_KIND_USER); asm_call(r_alloc); e_setc(RV_T1);   /* payload */
    asm_lit(R0_NONE); e_cell(RV_T1); asm_lit(ERR_DESC); asm_host(HOST_ADD); asm_store();
    asm_lit(ERR_NFIELDS); e_cell(RV_T1); asm_lit(ERR_COUNT); asm_host(HOST_ADD); asm_store();
    e_cell(RV_T1); asm_lit(ERR_ARG); asm_host(HOST_ADD); asm_store();      /* arg  */
    e_cell(RV_T1); asm_lit(ERR_ID); asm_host(HOST_ADD); asm_store();       /* id   */
    e_cell(RV_T1); asm_lit(ERR_TYPE); asm_host(HOST_ADD); asm_store();     /* type */
    e_cell(RV_T1); asm_lit(T_ERROR); asm_host(HOST_ADD);
    asm_exit();
}

/* JUDGE-LANDING: RAISE-UNWIND has restored the judge frame's caller state and
 * put the caller's return address in RV_ERRRET. Deliver the SIN! as JUDGE's
 * single result and continue in the caller. It is emitted directly after
 * MKERROR's EXIT, so it is never the return address of any invocation; that
 * is what lets RAISE-UNWIND and RETURN recognise a judge frame by its IP. */
static void emit_trap_landing(void) {
    r_trap_landing = asm_here();
    e_cell(RV_ERRV); asm_lit(R0_NONE); asm_host(HOST_EQ);
    cell j_have = asm_zbranch_fwd();          /* RV_ERRV != none: a raised value */
    e_cell(RV_ERRT); e_cell(RV_ERRI); e_cell(RV_ERRA);
    asm_call(r_mkerror);
    cell j_built = asm_branch_fwd();
    asm_patch_here(j_have);
    e_cell(RV_ERRV);
    asm_patch_here(j_built);
    asm_lit(16);                              /* one result */
    e_cell(RV_ERRRET); asm_lit(REG_IP); asm_store();
}

/* RAISE-UNWIND: find the nearest judge frame on the current world's frame
 * chain and restore it; with none, halt as an uncaught error. The data and
 * return stacks above the judge are discarded wholesale, so the operation that
 * raised (a store, a bind, a result delivery) is abandoned, never completed. */
static void emit_raise_unwind(void) {
    r_raise_unwind = asm_here();
    e_cell(RV_FRAME); e_setc(RV_T2);
    cell loop = asm_here();
    e_cell(RV_T2); asm_lit(0); asm_host(HOST_EQ);
    cell j_frame = asm_zbranch_fwd();
    asm_lit(1); e_setc(RV_ERRUNC);            /* uncaught: the old fail-stop */
    asm_host(HOST_DUMP); asm_halt();
    asm_patch_here(j_frame);
    e_cell(RV_T2); asm_lit(FRAME_IP); asm_host(HOST_ADD); asm_fetch();
    asm_lit(r_trap_landing); asm_host(HOST_EQ);
    cell j_notrap = asm_zbranch_fwd();
    e_cell(RV_T2); asm_lit(FRAME_SP); asm_host(HOST_ADD); asm_fetch(); asm_lit(REG_SP); asm_store();
    e_cell(RV_T2); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_CTX);
    e_cell(RV_T2); asm_lit(FRAME_CUR); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_CUR);
    e_cell(RV_T2); asm_lit(FRAME_END); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_END);
    e_cell(RV_T2); asm_lit(FRAME_BLK); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_BLK);
    e_cell(RV_T2); asm_lit(FRAME_TRAPRET); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_ERRRET);
    e_cell(RV_T2); asm_lit(FRAME_RP); asm_host(HOST_ADD); asm_fetch(); asm_lit(REG_RP); asm_store();
    e_cell(RV_T2); asm_fetch(); e_setc(RV_FRAME);          /* RV_FRAME = judge.prev */
    asm_lit(0); e_setc(RV_CHILD);             /* M2: abandoned invocations' roots */
    asm_lit(0); e_setc(RV_CLOSURE);
    asm_branch(r_trap_landing);
    asm_patch_here(j_notrap);
    e_cell(RV_T2); asm_fetch(); e_setc(RV_T2);             /* frame = prev */
    asm_branch(loop);
}

/* RAISE3: ( type id arg -- ) raise a runtime error built from these fields. */
static void emit_raise3(void) {
    r_raise3 = asm_here();
    e_setc(RV_ERRA); e_setc(RV_ERRI); e_setc(RV_ERRT);
    asm_lit(R0_NONE); e_setc(RV_ERRV);
    asm_branch(r_raise_unwind);
}

/* RAISE-ESCAPE: ( id -- ) raise SIN! [type 'escape, id, arg = the decoded
 * site id of the activation-dependent block in RV_ESC_A, as an integer]. The
 * block itself is never placed in the error: the raise abandons the
 * transport, so a judge recovers control without receiving the illegal value. */
static void emit_raise_escape_routine(void) {
    r_raise_escape = asm_here();
    e_setc(RV_ERRI);
    e_cell(RV_ESC_A); e_untag_ptr(); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch();
    asm_neg(); asm_lit(1); asm_host(HOST_SUB);          /* decoded site */
    asm_lit(16); asm_host(HOST_MUL); e_setc(RV_ERRA);    /* as a Glon integer */
    emit_word_lit("escape"); e_setc(RV_ERRT);
    asm_lit(R0_NONE); e_setc(RV_ERRV);
    asm_branch(r_raise_unwind);
}

/* Emitted right after the collector (MKERROR needs r_alloc) and before every
 * routine that can raise, so their addresses are known to all raise sites.
 * JUDGE-LANDING must directly follow MKERROR's EXIT (see its comment). */
static void emit_error_core(void) {
    emit_mkerror();
    emit_trap_landing();
    emit_raise_unwind();
    emit_raise3();
    emit_raise_escape_routine();
}

/* Return-stack headroom check (machine limit, stays a fatal fail-stop): the
 * same bound computation as INVOKE-CLOSURE's guard, for the main world or the
 * running M1 task. Uses RV_T5/RV_T6. */
static void emit_rs_guard(cell margin) {
    asm_lit(REG_SP); asm_fetch(); e_setc(RV_T5);
    e_cell(RV_T5); asm_lit(R0S1_DS_INIT); asm_host(HOST_GT);
    cell j_main = asm_zbranch_fwd();
    e_cell(RV_T5); asm_lit(M1_ARENA_BASE); asm_host(HOST_SUB);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_DIV);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_MUL);
    asm_lit(M1_ARENA_BASE); asm_host(HOST_ADD);
    asm_lit(M1_DS_OFF); asm_host(HOST_ADD); e_setc(RV_T6);
    cell j_g = asm_branch_fwd();
    asm_patch_here(j_main);
    asm_lit(R0S1_DS_INIT); e_setc(RV_T6);
    asm_patch_here(j_g);
    asm_lit(REG_RP); asm_fetch(); asm_lit(margin); asm_host(HOST_SUB);
    e_cell(RV_T6); asm_host(HOST_LT);
    cell j_room = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();          /* return-stack exhaustion */
    asm_patch_here(j_room);
}

static void emit_gc(void) {
    emit_mark_push();
    emit_trace_ctx();
    emit_trace_closure();
    emit_trace_user();
    emit_trace_string();
    emit_trace_block();
    emit_mark_frame();
    emit_mark_value();
    emit_scan_values();
    emit_scan_closures();
    emit_collect();
    emit_alloc();
    for (int i = 0; i < n_fw_mv; i++) s1_set_mem(fw_mark_value[i], r_mark_value);
    for (int i = 0; i < n_fw_mf; i++) s1_set_mem(fw_mark_frame[i], r_mark_frame);
}

/* ========================== evaluator build ============================ */

static cell r_reduce, r_set, r_append, r_mkctx, r_mkclosure;
static cell r_load_lex, r_hash_insert, r_build_hash;
static cell r_values, r_run_block, r_native, r_invoke_closure, r_return, r_invoke_raw, r_subexpr, r_block_eval;
static cell r_reduce_block;
static cell r_binding_ctx;

/* REDUCE: [r1..rN, tagged-N] -> [r1] (or [NONE] if N==0) */
static void emit_reduce(void) {
    r_reduce = asm_here();
    /* [v1..vN, taggedN] -> single value (or NONE if N == 0). Keep N in RV_N and
     * use a fast path for the overwhelmingly common N == 1 case. */
    asm_lit(16); asm_host(HOST_DIV);            /* [v1..vN, N] */
    e_setc(RV_N);                               /* [v1..vN] */
    e_cell(RV_N); asm_lit(1); asm_host(HOST_EQ); /* (N == 1) */
    cell j_not1 = asm_zbranch_fwd();
    asm_exit();                                 /* [v1] */
    asm_patch_here(j_not1);
    e_cell(RV_N); asm_lit(0); asm_host(HOST_EQ); /* (N == 0) */
    cell j_not0 = asm_zbranch_fwd();
    asm_lit(R0_NONE); asm_exit();               /* N == 0 -> NONE */
    asm_patch_here(j_not0);
    e_cell(RV_N); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_N); /* N-1 */
    cell rloop = asm_here();
    e_cell(RV_N); asm_lit(0); asm_host(HOST_GT);
    cell jdone = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_N); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_N);
    asm_branch(rloop);
    asm_patch_here(jdone);
    asm_exit();
}

/* DISCARD is now emitted inline in block-eval (denser than a subroutine call). */

/* LOOKUP: (word -- value | -1 if unbound) via RV_CTX.
 * FIB-OPT-P5: when a context's binding count is below its cap, resolve via the
 * hash index (O(1) average); otherwise fall back to the ordered linear scan. */
static void emit_lookup(void) {
    r_lookup = asm_here();
#ifdef R0_S1_PROFILE
    pf_incr(PF_LOOKUP);          /* one logical word lookup */
#endif
    e_pop_to(RV_T1);
    e_cell(RV_CTX);
    cell outer = asm_here();
    e_dup(); asm_lit(R0_NONE); asm_host(HOST_EQ);
    cell j_continue = asm_zbranch_fwd();
    asm_drop(); asm_lit(-1); asm_exit();
    asm_patch_here(j_continue);
    e_dup(); e_untag_ptr(); e_setc(RV_T2);
    e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T3);   /* count */
    /* FIB-OPT-P8: hash lookup only when HASH_MIN <= count < cap; small contexts
     * (fib's arity-1 activations) use the ordered linear scan and never build
     * the hash index. */
    e_cell(RV_T2); asm_lit(CTX_CAP); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T5);  /* cap */
    e_cell(RV_T3); asm_lit(HASH_MIN); asm_host(HOST_GE);
    cell j_linear = asm_zbranch_fwd();
    e_cell(RV_T3); e_cell(RV_T5); asm_host(HOST_LT);
    cell j_linear2 = asm_zbranch_fwd();
    e_cell(RV_T1); asm_lit(16); asm_host(HOST_DIV); e_setc(RV_T4);               /* id */
    e_cell(RV_T4); e_cell(RV_T5); asm_host(HOST_MOD); e_setc(RV_T6);             /* idx */
    asm_lit(0); e_setc(RV_N);
    cell hloop = asm_here();
    e_cell(RV_N); e_cell(RV_T5); asm_host(HOST_LT);
    cell j_hmiss = asm_zbranch_fwd();
#ifdef R0_S1_PROFILE
    pf_incr(PF_HASH_PROBES);
#endif
    e_cell(RV_T2); asm_lit(CTX_DATA); asm_host(HOST_ADD);
    e_cell(RV_T5); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    e_cell(RV_T6); asm_host(HOST_ADD); asm_fetch();                             /* [ctx, e] */
    asm_dup(); asm_lit(HASH_EMPTY); asm_host(HOST_EQ);                          /* [ctx, e, empty?] */
    cell j_notempty = asm_zbranch_fwd();
    asm_drop();                                                                 /* [ctx] */
#ifdef R0_S1_PROFILE
    pf_incr(PF_HASH_MISSES);
#endif
    cell fw_miss = asm_branch_fwd();
    asm_patch_here(j_notempty);
    asm_dup(); asm_lit(256); asm_host(HOST_DIV);                                /* [ctx, e, key] */
    e_cell(RV_T4); asm_host(HOST_EQ);                                           /* [ctx, e, key==id] */
    cell j_hcollide = asm_zbranch_fwd();
    asm_lit(256); asm_host(HOST_MOD);                                           /* [ctx, slot] (slot = e % 256) */
    e_setc(RV_T5);                                                              /* [ctx] */
#ifdef R0_S1_PROFILE
    pf_incr(PF_HASH_HITS);
#endif
    e_cell(RV_T2); asm_lit(CTX_DATA + 1); asm_host(HOST_ADD);
    e_cell(RV_T5); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_fetch();                                                                /* [ctx, value] */
    e_setc(RV_T5); asm_drop(); e_cell(RV_T5);                                   /* [value] */
    asm_exit();
    asm_patch_here(j_hcollide);
    asm_drop();                                                                 /* [ctx, e] -> [ctx] */
#ifdef R0_S1_PROFILE
    pf_incr(PF_HASH_COLLISIONS);
#endif
    e_cell(RV_T6); asm_lit(1); asm_host(HOST_ADD); e_cell(RV_T5); asm_host(HOST_MOD); e_setc(RV_T6);
    e_cell(RV_N); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_N);
    asm_branch(hloop);
    asm_patch_here(j_hmiss);
    asm_patch_here(fw_miss);
    cell fw_parent = asm_branch_fwd();
    /* linear fallback (count < HASH_MIN or count >= cap) */
    asm_patch_here(j_linear);
    asm_patch_here(j_linear2);
#ifdef R0_S1_PROFILE
    pf_incr(PF_HASH_FALLBACK);
#endif
    asm_lit(0); e_setc(RV_T4);
    cell inner = asm_here();
#ifdef R0_S1_PROFILE
    pf_incr(PF_LK_SLOTS);        /* examine one binding slot per iteration */
    pf_incr(PF_HASH_FALLBACK_SLOTS);
#endif
    e_cell(RV_T4); e_cell(RV_T3); asm_host(HOST_GE);
    cell j_search = asm_zbranch_fwd();
    /* move to parent (shared by hash miss + linear exhaustion) */
    asm_patch_here(fw_parent);
    asm_drop();
    e_cell(RV_T2); asm_fetch();
#ifdef R0_S1_PROFILE
    pf_incr(PF_LK_PARENT);       /* move to parent context */
#endif
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

/* BINDING-CTX: ( site -- origin-ctx 1 | 0 0 )  the "Guard of Binding" origin
 * lookup for a COMPUTED func body (one that was not parsed as that func's own
 * literal body, so its T_BOUND depths are relative to an enclosing activation).
 * Walk the frame chain innermost-first for the nearest live frame carrying
 * `site`; its child context is RV_CTX when it is the innermost frame, else the
 * FRAME_CTX saved by the frame called directly from it.
 *   - bias-0 frame (the origin activation itself): origin = its child context;
 *   - bias-1 frame (a closure manufactured from one of the origin's blocks,
 *     e.g. `does [...]`): origin = its child context's parent, which is the
 *     origin context that closure captured -- so a block from an escaped
 *     factory closure still resolves against ITS captured origin, not against
 *     some unrelated live activation of the same site.
 * Either way the new closure captures the origin with a +1 depth bias.
 * Returns (0, 0) when the site has no live frame: the block's lexical origin
 * is dead, and FUNC must not bind it to an unrelated context. */
static void emit_binding_ctx(void) {
    r_binding_ctx = asm_here();
    e_pop_to(RV_T1);                       /* site (raw) */
    e_cell(RV_FRAME); e_setc(RV_T4);       /* frame */
    asm_lit(0); e_setc(RV_T5);             /* prev = 0 */
    cell walk = asm_here();
    e_cell(RV_T4); asm_lit(0); asm_host(HOST_NE);
    cell j_notfound = asm_zbranch_fwd();
    e_cell(RV_T4); asm_lit(FRAME_SITE); asm_host(HOST_ADD); asm_fetch();
    e_cell(RV_T1); asm_host(HOST_EQ);
    cell j_next = asm_zbranch_fwd();
    /* matched: push the frame's child context (RV_CTX if innermost) */
    e_cell(RV_T5); asm_lit(0); asm_host(HOST_EQ);
    cell j_hasprev = asm_zbranch_fwd();
    e_cell(RV_CTX);
    cell j_gotchild = asm_branch_fwd();
    asm_patch_here(j_hasprev);
    e_cell(RV_T5); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_fetch();
    asm_patch_here(j_gotchild);
    /* bias-1 frame: step to the child's parent (the captured origin) */
    e_cell(RV_T4); asm_lit(FRAME_BIAS); asm_host(HOST_ADD); asm_fetch();
    asm_lit(0); asm_host(HOST_EQ);
    cell j_origin = asm_zbranch_fwd();
    asm_lit(1); asm_exit();                /* bias-0: [child 1] */
    asm_patch_here(j_origin);
    e_untag_ptr(); asm_lit(CTX_PARENT); asm_host(HOST_ADD); asm_fetch();
    asm_lit(1); asm_exit();                /* bias-1: [parent 1] */
    asm_patch_here(j_next);
    e_cell(RV_T4); e_setc(RV_T5);          /* prev = frame */
    e_cell(RV_T4); asm_fetch(); e_setc(RV_T4);   /* frame = frame.FRAME_PREV */
    asm_branch(walk);
    asm_patch_here(j_notfound);
    asm_lit(0); asm_lit(0); asm_exit();
}

/* LOAD-LEX: ( bound -- value )  direct lexical slot access via RV_CTX.
 * A bound word encodes (depth, slot); this walks `depth` CTX_PARENT hops from
 * the current context and reads the value cell at the slot offset, with no
 * name comparison. Only parameter references are emitted as bound words, so
 * the slot is always live and the address is always sound. */
static void emit_load_lex(void) {
    r_load_lex = asm_here();
#ifdef R0_S1_PROFILE
    pf_incr(PF_LEX_DIRECT);                  /* one direct lexical access */
#endif
    /* Bound-block safety guard: a T_BOUND reference is relative to the
     * activation that created its block. A bare block records only its static
     * BLK_SITE, so it is valid only while executing under a frame with the same
     * FRAME_SITE (and never with no frame at all). A travelling bound block --
     * e.g. passed into a helper and `do`ne there, or `do`ne after its creator
     * returned -- would otherwise silently resolve against an unrelated
     * activation, so it must fail loudly. Closures (which capture CLOSURE_CTX)
     * are the portable form and are unaffected. */
    e_cell(RV_FRAME); asm_lit(0); asm_host(HOST_NE);   /* frame != 0 */
    e_cell(RV_BLK); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch();
    emit_decode_site();                                /* dep-encoded -> site id */
    e_cell(RV_FRAME); asm_lit(FRAME_SITE); asm_host(HOST_ADD); asm_fetch();
    asm_host(HOST_EQ);                                  /* block site == frame site */
    asm_host(HOST_MUL);                                 /* guard */
    cell j_guard_ok = asm_zbranch_fwd();
    asm_lit(16); asm_host(HOST_DIV);         /* payload = depth*16+slot (tag drops out) */
    asm_dup(); asm_lit(16); asm_host(HOST_MOD); e_setc(RV_T2);  /* slot  */
    asm_lit(16); asm_host(HOST_DIV); e_setc(RV_T3);             /* depth */
    /* Guard of Binding: add the current activation's depth bias (0 for a
     * hand-written FUNC body, +1 for a factory-manufactured body). The bias
     * lives in the frame, so it is correctly scoped across nested invocations
     * (each activation's frame carries its own). RV_FRAME is 0 only at top
     * level, where no T_BOUND reference can occur. */
    e_cell(RV_FRAME); asm_lit(0); asm_host(HOST_NE);
    cell j_nobias = asm_zbranch_fwd();
    e_cell(RV_FRAME); asm_lit(FRAME_BIAS); asm_host(HOST_ADD); asm_fetch();
    e_cell(RV_T3); asm_host(HOST_ADD); e_setc(RV_T3);           /* depth += bias */
    asm_patch_here(j_nobias);
#ifdef R0_S1_PROFILE
    e_cell(RV_T3); asm_lit(0); asm_host(HOST_EQ);
    cell j_parent = asm_zbranch_fwd();
    pf_incr(PF_LEX_LOCAL);                   /* depth 0 (own parameter) */
    cell j_lex = asm_branch_fwd();
    asm_patch_here(j_parent);
    pf_incr(PF_LEX_PARENT);                  /* depth > 0 (captured) */
    asm_patch_here(j_lex);
#endif
    e_cell(RV_CTX);                          /* ctx (tagged) */
    cell walk = asm_here();
    e_cell(RV_T3); asm_lit(0); asm_host(HOST_GT);
    cell j_walk_done = asm_zbranch_fwd();
    e_untag_ptr(); asm_lit(CTX_PARENT); asm_host(HOST_ADD); asm_fetch(); /* [ctx] -> [parent] */
    e_cell(RV_T3); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_T3);
    asm_branch(walk);
    asm_patch_here(j_walk_done);
    e_untag_ptr(); asm_lit(CTX_DATA + 1); asm_host(HOST_ADD);            /* [ctx] -> [p+4] */
    e_cell(RV_T2); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);   /* +2*slot */
    asm_fetch();                              /* [value] */
    asm_exit();
    /* invalid bound execution: no frame, or block site != frame site */
    asm_patch_here(j_guard_ok);
    asm_host(HOST_DUMP); asm_halt();
}

/* HASH-INSERT (P5): insert (RV_WORD -> RV_T3 slot) into the hash index of the
 * context whose payload is RV_T2. Skipped when slot >= cap (index full/overflow;
 * the linear fallback then stays correct). Uses RV_T1/RV_T5/RV_T6/RV_N (NOT
 * RV_T4: the callers -- the bind loop and r_set -- keep their loop counter
 * there). */
static void emit_hash_insert(void) {
    r_hash_insert = asm_here();
    e_cell(RV_T2); asm_lit(CTX_CAP); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T1);  /* cap */
    e_cell(RV_WORD); asm_lit(16); asm_host(HOST_DIV); e_setc(RV_T5);                  /* id */
    e_cell(RV_T3); e_cell(RV_T1); asm_host(HOST_LT);                                  /* slot < cap? */
    cell j_skip = asm_zbranch_fwd();
    e_cell(RV_T5); e_cell(RV_T1); asm_host(HOST_MOD); e_setc(RV_T6);                  /* idx = id%cap */
    asm_lit(0); e_setc(RV_N);
    cell hloop = asm_here();
    e_cell(RV_N); e_cell(RV_T1); asm_host(HOST_LT);                                   /* k < cap? */
    cell j_hdone = asm_zbranch_fwd();
    e_cell(RV_T2); asm_lit(CTX_DATA); asm_host(HOST_ADD);
    e_cell(RV_T1); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    e_cell(RV_T6); asm_host(HOST_ADD); asm_fetch();                                   /* [e] */
    asm_dup(); asm_lit(HASH_EMPTY); asm_host(HOST_EQ);                                /* [e, empty?] */
    cell j_notempty = asm_zbranch_fwd();
    asm_drop();                                                                       /* [] */
    e_cell(RV_T5); asm_lit(256); asm_host(HOST_MUL); e_cell(RV_T3); asm_host(HOST_ADD); /* id*256+slot */
    e_cell(RV_T2); asm_lit(CTX_DATA); asm_host(HOST_ADD);
    e_cell(RV_T1); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    e_cell(RV_T6); asm_host(HOST_ADD); asm_store();                                   /* M[ho+idx] = entry */
    cell j_inserted = asm_branch_fwd();
    asm_patch_here(j_notempty);
    asm_drop();                                                                       /* drop e */
    e_cell(RV_T6); asm_lit(1); asm_host(HOST_ADD); e_cell(RV_T1); asm_host(HOST_MOD); e_setc(RV_T6);
    e_cell(RV_N); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_N);
    asm_branch(hloop);
    asm_patch_here(j_inserted);
    asm_patch_here(j_hdone);
    asm_patch_here(j_skip);
    asm_exit();
}

/* BUILD-HASH: build the P5 hash index for context payload RV_T2 with count
 * RV_T3. Zeroes `cap` buckets, then inserts each of the `count` (word,value)
 * pairs via r_hash_insert. Called once when a context's binding count reaches
 * HASH_MIN (the hash is otherwise never materialised). */
static void emit_build_hash(void) {
    r_build_hash = asm_here();
    e_cell(RV_T2); asm_lit(CTX_CAP); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T1);  /* cap */
    /* zero cap buckets: M[ctx + CTX_DATA + 2*cap + i] = HASH_EMPTY */
    asm_lit(0); e_setc(RV_N);
    cell zloop = asm_here();
    e_cell(RV_N); e_cell(RV_T1); asm_host(HOST_LT);
    cell j_zdone = asm_zbranch_fwd();
    asm_lit(HASH_EMPTY);
    e_cell(RV_T2); asm_lit(CTX_DATA); asm_host(HOST_ADD);
    e_cell(RV_T1); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    e_cell(RV_N); asm_host(HOST_ADD);
    asm_store();
    e_cell(RV_N); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_N);
    asm_branch(zloop);
    asm_patch_here(j_zdone);
    /* insert each pair: for i in [0, count), RV_WORD = data[2i], slot = i.
     * RV_SITE is the loop counter: it is written only by r_return and is never
     * live during binding, so using it here preserves the caller's RV_T4. */
    e_cell(RV_T3); asm_toR();                    /* save count on R (hash_insert clobbers RV_T3's role) */
    asm_lit(0); e_setc(RV_SITE);                 /* i = 0 */
    cell iloop = asm_here();
    e_cell(RV_SITE); asm_fetchR(); asm_host(HOST_LT);  /* i < count? */
    cell j_idone = asm_zbranch_fwd();
    e_cell(RV_T2); asm_lit(CTX_DATA); asm_host(HOST_ADD);
    e_cell(RV_SITE); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_fetch(); e_setc(RV_WORD);                /* RV_WORD = data[2i] */
    e_cell(RV_SITE); e_setc(RV_T3);              /* slot = i */
    asm_call(r_hash_insert);
    e_cell(RV_SITE); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_SITE);   /* i++ */
    asm_branch(iloop);
    asm_patch_here(j_idone);
    asm_fromR(); asm_drop();                     /* pop saved count */
    asm_exit();
}

/* SET: (value -- value)  bind word (RV_WORD) to value, nearest-update */
/* ==================== escape-time binding law enforcement ==================
 * A block whose executable tree carries a T_BOUND/RETURN that resolves against
 * its origin activation is "activation-dependent" and marked (parser) by a
 * negated BLK_SITE. It may be consumed or closed over while that activation is
 * live, but must not be transported beyond it. These routines raise SIN! 'escape at the
 * transport boundary; they never resolve, copy or evaluate the value. They use
 * only the private RV_ESC_* cells (never SCRATCH_A..F, GC/M1 state, the D1 inspection state or
 * any blessed RAW library cell), so a legal check has zero observable effect on
 * program state. */

/* A detected violation raises SIN! [type 'escape, id <boundary>, arg <the
 * decoded site id of the offending block, as an integer>]. The block itself is
 * never placed in the error: the raise abandons the transport, so a judge
 * recovers control without ever receiving the illegal value. Uncaught, the
 * raise halts exactly as the old fail-stop did. `id` NULL keeps RV_ERRI as
 * preset by the caller (the shared ESC-NOTANC check serves store and capture). */
static void emit_raise_escape(const char *id) {
    if (id) emit_word_lit(id); else e_cell(RV_ERRI);
    asm_branch(r_raise_escape);
}

/* ESC-ANY: raise if RV_ESC_A holds an activation-dependent block. */
static void emit_esc_any(void) {
    r_esc_any = asm_here();
    e_cell(RV_ESC_A); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_notblk = asm_zbranch_fwd();
    e_cell(RV_ESC_A); e_untag_ptr(); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch();
    asm_lit(0); asm_host(HOST_LT);
    cell j_nodep = asm_zbranch_fwd();
    emit_raise_escape("transport");
    asm_patch_here(j_nodep);
    asm_patch_here(j_notblk);
    asm_exit();
}

/* ESC-TRANSPORT: ( body -- body ) the escape law's task-transport boundary (5),
 * the one check every blessed RAW task-creation word (mnew-task) calls: raise
 * 'escape 'transport if the TAGGED value on top of the data stack is an
 * activation-dependent block. It must be called while the body is still
 * tagged and before any task-table or scheduler state changes, so a rejected
 * transport creates no task and runs no body instruction. */
static void emit_esc_transport(void) {
    r_esc_transport = asm_here();
    e_peek(); asm_lit(RV_ESC_A); asm_store();
    asm_branch(r_esc_any);                          /* tail: ESC-ANY's EXIT returns */
}

/* ESC-NOTANC: RV_ESC_A = value, RV_ESC_B = context/site owner. Raise if
 * the value is an activation-dependent block whose site B is NOT an
 * ancestor-or-self of the owner (a foreign-site store/capture). */
static void emit_esc_eq(void) {
    r_esc_eq = asm_here();
    e_cell(RV_ESC_A); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_done1 = asm_zbranch_fwd();
    e_cell(RV_ESC_A); e_untag_ptr(); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch();
    asm_dup(); asm_lit(0); asm_host(HOST_LT);
    cell j_nonneg = asm_zbranch_fwd();
    asm_neg(); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_ESC_N);      /* B */
    e_cell(RV_ESC_B); e_setc(RV_ESC_W);                               /* walk copy of owner */
    cell loop = asm_here();
    e_cell(RV_ESC_N); e_cell(RV_ESC_W); asm_host(HOST_EQ);
    cell j_neq = asm_zbranch_fwd();
    asm_exit();                                                    /* B == D: allowed */
    asm_patch_here(j_neq);
    e_cell(RV_ESC_W); asm_lit(0); asm_host(HOST_EQ);
    cell j_nonzero = asm_zbranch_fwd();
    emit_raise_escape(NULL);                                       /* root: not ancestor */
    asm_patch_here(j_nonzero);
    e_cell(RV_ESC_W); asm_lit(SITE_PARENT_BASE); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_ESC_W);
    asm_branch(loop);
    asm_patch_here(j_nonneg);
    asm_drop(); asm_exit();
    asm_patch_here(j_done1);
    asm_exit();
}

/* ESC-ANC: RV_ESC_A = value, RV_ESC_B = returning frame site F. Raise if
 * the value is an activation-dependent block whose site B is ancestor-or-self
 * of F (a frame of B or nested in B must not return B's block). */
static void emit_esc_anc(void) {
    r_esc_anc = asm_here();
    e_cell(RV_ESC_A); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_done1 = asm_zbranch_fwd();
    e_cell(RV_ESC_A); e_untag_ptr(); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch();
    asm_dup(); asm_lit(0); asm_host(HOST_LT);
    cell j_nonneg = asm_zbranch_fwd();
    asm_neg(); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_ESC_N);      /* B */
    e_cell(RV_ESC_B); e_setc(RV_ESC_W);                               /* walk copy of F */
    cell loop = asm_here();
    e_cell(RV_ESC_N); e_cell(RV_ESC_W); asm_host(HOST_EQ);
    cell j_neq = asm_zbranch_fwd();
    emit_raise_escape("return");                                   /* escape */
    asm_patch_here(j_neq);
    e_cell(RV_ESC_W); asm_lit(0); asm_host(HOST_EQ);
    cell j_nonzero = asm_zbranch_fwd();
    asm_exit();                                                    /* root: allowed */
    asm_patch_here(j_nonzero);
    e_cell(RV_ESC_W); asm_lit(SITE_PARENT_BASE); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_ESC_W);
    asm_branch(loop);
    asm_patch_here(j_nonneg);
    asm_drop(); asm_exit();
    asm_patch_here(j_done1);
    asm_exit();
}

/* ESC-RESULTS: fail-stop if any value in the current data-stack result set
 * [v0..vN-1, taggedN] is an activation-dependent block whose site is
 * ancestor-or-self of the returning frame's site. SCRATCH_C keeps that frame
 * site across the per-value ancestry walks (RV_ESC_C). */
static void emit_esc_results(void) {
    r_esc_results = asm_here();
    e_cell(RV_FRAME); asm_lit(FRAME_SITE); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_ESC_C);  /* F */
    e_peek(); asm_lit(16); asm_host(HOST_DIV); e_setc(RV_ESC_E);   /* N */
    asm_lit(0); e_setc(RV_ESC_F);
    cell loop = asm_here();
    e_cell(RV_ESC_F); e_cell(RV_ESC_E); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    asm_lit(REG_SP); asm_fetch(); asm_lit(1); asm_host(HOST_ADD);
    e_cell(RV_ESC_F); asm_host(HOST_ADD); asm_fetch();             /* v_i */
    asm_lit(RV_ESC_A); asm_store();
    e_cell(RV_ESC_C); asm_lit(RV_ESC_B); asm_store();          /* reset F */
    asm_call(r_esc_anc);
    e_cell(RV_ESC_F); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_ESC_F);
    asm_branch(loop);
    asm_patch_here(j_done);
    asm_exit();
}

/* ESC-CAPTURE: RV_T1 = a context about to be captured/promoted. Raise if it
 * holds any activation-dependent block whose site != the context's owning site
 * (global context = owner site 0). */
static void emit_esc_capture(void) {
    r_esc_capture = asm_here();
    emit_word_lit("capture"); e_setc(RV_ERRI);       /* id if ESC-NOTANC raises */
    e_cell(RV_T1); e_untag_ptr(); e_setc(RV_ESC_P);                        /* p */
    e_cell(RV_T1); e_cell(GC_GLOBAL_CTX); asm_host(HOST_EQ);
    cell j_nglobal = asm_zbranch_fwd();
    asm_lit(0); asm_lit(RV_ESC_B); asm_store();
    cell j_ga = asm_branch_fwd();
    asm_patch_here(j_nglobal);
    e_cell(RV_ESC_P); asm_lit(CTX_ESCSITE); asm_host(HOST_ADD); asm_fetch(); asm_lit(RV_ESC_B); asm_store();
    asm_patch_here(j_ga);
    e_cell(RV_ESC_P); asm_lit(CTX_COUNT); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_ESC_E);
    asm_lit(0); e_setc(RV_ESC_F);
    cell loop = asm_here();
    e_cell(RV_ESC_F); e_cell(RV_ESC_E); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    e_cell(RV_ESC_P); asm_lit(CTX_DATA + 1); asm_host(HOST_ADD);
    e_cell(RV_ESC_F); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD); asm_fetch();
    asm_lit(RV_ESC_A); asm_store();
    asm_call(r_esc_eq);
    e_cell(RV_ESC_F); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_ESC_F);
    asm_branch(loop);
    asm_patch_here(j_done);
    asm_exit();
}

/* ESC-STORE-CHECK: r_set has located the destination context payload in RV_T2
 * and the value to store in RV_T6. */
static void emit_esc_store_check(void) {
    e_cell(RV_T6); asm_lit(RV_ESC_A); asm_store();
    e_cell(RV_T2); asm_lit(T_CONTEXT); asm_host(HOST_ADD);
    e_cell(GC_GLOBAL_CTX); asm_host(HOST_EQ);
    cell j_nglobal = asm_zbranch_fwd();
    asm_lit(0); asm_lit(RV_ESC_B); asm_store();
    cell j_ga = asm_branch_fwd();
    asm_patch_here(j_nglobal);
    e_cell(RV_T2); asm_lit(CTX_ESCSITE); asm_host(HOST_ADD); asm_fetch(); asm_lit(RV_ESC_B); asm_store();
    asm_patch_here(j_ga);
    emit_word_lit("store"); e_setc(RV_ERRI);         /* id if ESC-NOTANC raises */
    emit_esc_ref(&r_esc_eq);
}

/* ESC-SAME: RV_ESC_A = value, RV_ESC_B = closure site. Raise only if the
 * value is an activation-dependent block whose decoded site == SCRATCH_B
 * (same-site argument re-entry). A foreign-site dependent block passed down to
 * a live helper remains legal. */
static void emit_esc_same(void) {
    r_esc_same = asm_here();
    e_cell(RV_ESC_A); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_notblk = asm_zbranch_fwd();
    e_cell(RV_ESC_A); e_untag_ptr(); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch();
    asm_dup(); asm_lit(0); asm_host(HOST_LT);
    cell j_nonneg = asm_zbranch_fwd();
    asm_neg(); asm_lit(1); asm_host(HOST_SUB);      /* decoded site */
    e_cell(RV_ESC_B); asm_host(HOST_EQ);        /* == closure site? */
    cell j_neq = asm_zbranch_fwd();                 /* 0 -> not equal -> permitted */
    emit_raise_escape("argument");                  /* same-site re-entry */
    asm_patch_here(j_neq);
    asm_exit();
    asm_patch_here(j_nonneg);
    asm_drop();                                     /* raw site was not dep */
    asm_exit();
    asm_patch_here(j_notblk);
    asm_exit();
}

/* CONTEXT-FULL guard (inline, shared by r_set and r_append): RV_T2 = context
 * payload, RV_T3 = its binding count. If the context is full, fail-stop BEFORE
 * any write: record R0S1_HALT_CONTEXT_FULL, dump and HALT. This is a machine
 * resource ceiling, not a SIN! (no judge can catch it), so a full context never
 * overwrites its hash index / escape-site tail or whatever follows it. */
static void emit_ctx_full_guard(void) {
    e_cell(RV_T3);
    e_cell(RV_T2); asm_lit(CTX_CAP); asm_host(HOST_ADD); asm_fetch();
    asm_host(HOST_GE);                                   /* count >= cap ? */
    cell j_room = asm_zbranch_fwd();
    asm_lit(R0S1_HALT_CONTEXT_FULL); e_setc(RV_HALTWHY);
    asm_host(HOST_DUMP); asm_halt();
    asm_patch_here(j_room);
}

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
    emit_ctx_full_guard();                   /* no room: fail-stop before writing */
    e_cell(RV_WORD);
    e_cell(RV_T2); asm_lit(3); asm_host(HOST_ADD);
    e_cell(RV_T3); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_store();
    emit_esc_store_check();
    e_cell(RV_T6);
    e_cell(RV_T2); asm_lit(4); asm_host(HOST_ADD);
    e_cell(RV_T3); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    asm_store();
    e_cell(RV_T3); asm_lit(1); asm_host(HOST_ADD);
    e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD);
    asm_store();
    /* FIB-OPT-P8: maintain the hash index only once count >= HASH_MIN */
    e_cell(RV_T3); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_T5);   /* new count */
    e_cell(RV_T5); asm_lit(HASH_MIN); asm_host(HOST_GE);
    cell j_nohash = asm_zbranch_fwd();
    e_cell(RV_T5); asm_lit(HASH_MIN); asm_host(HOST_EQ);
    cell j_insert = asm_zbranch_fwd();
    e_cell(RV_T5); e_setc(RV_T3);            /* RV_T3 = count (for build_hash) */
    asm_call(r_build_hash);
    cell j_done = asm_branch_fwd();
    asm_patch_here(j_insert);
    asm_call(r_hash_insert);                 /* RV_T3 still = slot */
    asm_patch_here(j_done);
    asm_patch_here(j_nohash);
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
    emit_esc_store_check();
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
    emit_ctx_full_guard();                   /* no room: fail-stop before writing */
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
    /* FIB-OPT-P8: maintain the hash index only once count >= HASH_MIN */
    e_cell(RV_T3); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_T5);   /* new count = RV_T3+1 */
    e_cell(RV_T5); asm_lit(HASH_MIN); asm_host(HOST_GE);
    cell j_nohash = asm_zbranch_fwd();
    e_cell(RV_T5); asm_lit(HASH_MIN); asm_host(HOST_EQ);
    cell j_insert = asm_zbranch_fwd();
    e_cell(RV_T5); e_setc(RV_T3);            /* RV_T3 = count (for build_hash) */
    asm_call(r_build_hash);
    cell j_done = asm_branch_fwd();
    asm_patch_here(j_insert);
    asm_call(r_hash_insert);                 /* RV_T3 still = slot */
    asm_patch_here(j_done);
    asm_patch_here(j_nohash);
    asm_exit();
}

/* MKCTX: ( parent -- child-ctx )  allocate a fresh context.
 * Since FIB-OPT-P3 an ordinary (non-escaping) child context lives in task-local
 * storage on the return stack (16-aligned, same layout as a managed context);
 * it is promoted to the managed heap only if a closure captures it
 * (see emit_promote_ctx / emit_mkclosure). FIB-OPT-P5 adds the hash index
 * (cap entries after the data) and zeroes it to HASH_EMPTY. */
static void emit_mkctx(void) {
    r_mkctx = asm_here();
#ifdef R0_S1_PROFILE
    pf_incr(PF_ALLOC_CTX);
#endif
    e_pop_to(RV_T6);                                   /* parent */
    /* padding = (RP - R0S1_CTX_CELLS) mod 16 */
    asm_lit(REG_RP); asm_fetch(); asm_lit(R0S1_CTX_CELLS); asm_host(HOST_SUB); asm_lit(16); asm_host(HOST_MOD); e_setc(RV_T5);
    /* RP -= padding + R0S1_CTX_CELLS  (base is now 16-aligned) */
    asm_lit(REG_RP); asm_fetch(); e_cell(RV_T5); asm_host(HOST_SUB); asm_lit(R0S1_CTX_CELLS); asm_host(HOST_SUB); asm_lit(REG_RP); asm_store();
    asm_lit(REG_RP); asm_fetch(); e_setc(RV_T4);       /* base */
    e_cell(RV_T6); e_cell(RV_T4); asm_store();          /* M[base] = parent */
    asm_lit(0); e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); asm_store(); /* count = 0 */
    asm_lit(R0S1_CTX_CAP); e_cell(RV_T4); asm_lit(2); asm_host(HOST_ADD); asm_store(); /* cap */
    /* Escape law: record the owning func-site of this context (the invoking
     * closure's site) in the unused context tail. */
    e_cell(RV_CLOSURE); asm_lit(CLOSURE_SITE); asm_host(HOST_ADD); asm_fetch();
    e_cell(RV_T4); asm_lit(CTX_ESCSITE); asm_host(HOST_ADD); asm_store();

    /* FIB-OPT-P8: the hash index is NOT zeroed here. It is built lazily by
     * r_build_hash when the context's binding count reaches HASH_MIN. Until
     * then lookups use the ordered linear scan. */
    e_cell(RV_T4); asm_lit(T_CONTEXT); asm_host(HOST_ADD);
    /* Move the CALL return address from above the context (base+padding+CELLS)
     * to just below it (base-1), and leave RP at base-1. asm_exit then pops the
     * moved return address and RP becomes base, so the CELLS+padding-cell
     * context reservation stays in place for the caller (the frame is allocated
     * below it, never overlapping). */
    e_cell(RV_T4); e_cell(RV_T5); asm_host(HOST_ADD); asm_lit(R0S1_CTX_CELLS); asm_host(HOST_ADD); asm_fetch();
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_SUB); asm_store();
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_SUB); asm_lit(REG_RP); asm_store();
    asm_exit();
}

/* MKCLOSURE: ( spec body captured site-id bias -- closure ).
 * Promotes the captured context to the managed heap when it is stack-local
 * (escaping), so the closure always captures a managed/loader context. The
 * bias (0 or +1) is stored in CLOSURE_BIAS and copied to the frame on
 * invocation, where LOAD-LEX adds it to each T_BOUND depth.
 *
 * Closure-promotion note (recorded, deliberately NOT optimised): a factory-made
 * closure whose body has no lexical reference still promotes a stack-local
 * captured context (closure 32 + context 80 managed cells).  The obvious
 * shortcut -- "no T_BOUND in the body, so skip promotion" -- is UNSOUND: a plain
 * T_WORD reference in the body may denote a local introduced by SET-WORD in the
 * enclosing activation, which is resolved through the captured context, not by
 * T_BOUND.  Distinguishing such locals from globals would require escape
 * analysis, so promotion remains unconditional here. */
static void emit_mkclosure(void) {
    r_mkclosure = asm_here();
    /* GC-safepoint invariant: site-id and bias are raw implementation values,
     * not tagged Glon values. Pop them off the DS BEFORE any allocation so a GC
     * triggered by it never scans them as Glon values. spec and body stay on
     * the DS: both are valid tagged Glon values (blocks).
     *
     * Allocation order: the captured context is promoted FIRST and the closure
     * allocated LAST. The new closure is reachable from no root until this
     * routine returns it on the DS, so a GC triggered by a later allocation
     * (the promotion) would sweep it and hand out a freed object. */
    e_pop_to(RV_T6);  /* bias (raw) */
    e_pop_to(RV_T5);  /* site-id (raw) */
    e_pop_to(RV_T1);  /* captured */
    /* escape law (4): the context about to be captured/promoted must not hold
     * an activation-dependent block of a foreign site. */
    emit_esc_ref(&r_esc_capture);
    /* promote captured (RV_T1) if it is a stack-local context */
    {
        e_cell(RV_T1); asm_lit(T_CONTEXT); asm_host(HOST_SUB); e_setc(RV_T2);  /* p */
        e_cell(RV_T2); asm_lit(R0S1_DS_INIT); asm_host(HOST_GE);
        cell j_m1 = asm_zbranch_fwd();
        e_cell(RV_T2); asm_lit(R0S1_RS_INIT); asm_host(HOST_LT);
        cell j_m1b = asm_zbranch_fwd();
        cell b_prom = asm_branch_fwd();
        asm_patch_here(j_m1b);
        asm_patch_here(j_m1);
        e_cell(RV_T2); asm_lit(M1_ARENA_BASE); asm_host(HOST_GE);
        cell j_no1 = asm_zbranch_fwd();
        e_cell(RV_T2); asm_lit(M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS); asm_host(HOST_LT);
        cell j_no2 = asm_zbranch_fwd();
        asm_patch_here(b_prom);
#ifdef R0_S1_PROFILE
        pf_incr(PF_PROMOTE);
#endif
        asm_lit(R0S1_CTX_CELLS); asm_lit(GC_KIND_CTX); asm_call(r_alloc); e_setc(RV_T3);   /* managed dst */
        e_cell(RV_T2); asm_fetch(); e_cell(RV_T3); asm_store();                /* parent */
        e_cell(RV_T2); asm_lit(1); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T4); /* count */
        e_cell(RV_T4); e_cell(RV_T3); asm_lit(1); asm_host(HOST_ADD); asm_store();
        e_cell(RV_T2); asm_lit(2); asm_host(HOST_ADD); asm_fetch(); e_cell(RV_T3); asm_lit(2); asm_host(HOST_ADD); asm_store();
        /* copy the owning-site marker into the managed copy */
        e_cell(RV_T2); asm_lit(CTX_ESCSITE); asm_host(HOST_ADD); asm_fetch();
        e_cell(RV_T3); asm_lit(CTX_ESCSITE); asm_host(HOST_ADD); asm_store();

        asm_lit(0); e_setc(RV_N);                       /* i = 0 */
        cell loop = asm_here();
        e_cell(RV_N); e_cell(RV_T4); asm_host(HOST_LT);
        cell j_done = asm_zbranch_fwd();
        e_cell(RV_T2); asm_lit(3); asm_host(HOST_ADD); e_cell(RV_N); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD); asm_fetch();
        e_cell(RV_T3); asm_lit(3); asm_host(HOST_ADD); e_cell(RV_N); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD); asm_store();
        e_cell(RV_T2); asm_lit(4); asm_host(HOST_ADD); e_cell(RV_N); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD); asm_fetch();
        e_cell(RV_T3); asm_lit(4); asm_host(HOST_ADD); e_cell(RV_N); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD); asm_store();
        e_cell(RV_N); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_N);
        asm_branch(loop);
        asm_patch_here(j_done);
        /* P5: copy the hash index (cap entries) */
        asm_lit(0); e_setc(RV_N);
        cell hloop = asm_here();
        e_cell(RV_N); asm_lit(R0S1_CTX_CAP); asm_host(HOST_LT);
        cell j_hdone = asm_zbranch_fwd();
        e_cell(RV_T2); asm_lit(CTX_HASH); asm_host(HOST_ADD); e_cell(RV_N); asm_host(HOST_ADD); asm_fetch();
        e_cell(RV_T3); asm_lit(CTX_HASH); asm_host(HOST_ADD); e_cell(RV_N); asm_host(HOST_ADD); asm_store();
        e_cell(RV_N); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_N);
        asm_branch(hloop);
        asm_patch_here(j_hdone);
        e_cell(RV_T3); asm_lit(T_CONTEXT); asm_host(HOST_ADD); e_setc(RV_T3);  /* managed (tagged) */
        /* Redirect every live reference to the stack context onto its managed
         * copy, so the owning activation and the new closure share ONE context
         * (captured mutation is visible both ways). The stack context is
         * referenced only as RV_CTX (its activation is current) or as the
         * FRAME_CTX saved by the frame called from its activation (e.g. when a
         * factory such as `does` promotes its caller's context). It is never a
         * CTX_PARENT: a closure capturing it would already have promoted it.
         * RV_CTX must not be overwritten unconditionally -- the creator may be
         * a factory whose own context is unrelated to the captured one. */
        e_cell(RV_CTX); e_cell(RV_T1); asm_host(HOST_EQ);
        cell j_ctxdiff = asm_zbranch_fwd();
        e_cell(RV_T3); e_setc(RV_CTX);
        asm_patch_here(j_ctxdiff);
        e_cell(RV_FRAME); e_setc(RV_N);                 /* frame walk */
        cell rloop = asm_here();
        e_cell(RV_N); asm_lit(0); asm_host(HOST_NE);
        cell j_rdone = asm_zbranch_fwd();
        e_cell(RV_N); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_fetch();
        e_cell(RV_T1); asm_host(HOST_EQ);
        cell j_fsame = asm_zbranch_fwd();
        e_cell(RV_T3); e_cell(RV_N); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_store();
        asm_patch_here(j_fsame);
        e_cell(RV_N); asm_lit(FRAME_PREV); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_N);
        asm_branch(rloop);
        asm_patch_here(j_rdone);
        e_cell(RV_T3); e_setc(RV_T1);                   /* captured = managed */
        asm_patch_here(j_no2);
        asm_patch_here(j_no1);
    }
    /* keep captured rooted on the DS (with spec and body) across the closure
     * allocation */
    e_cell(RV_T1);
    asm_lit(16); asm_lit(GC_KIND_CLOSURE); asm_call(r_alloc); e_setc(RV_T4);   /* 16-aligned, >= 5 cells */
    e_cell(RV_T6); e_cell(RV_T4); asm_lit(CLOSURE_BIAS); asm_host(HOST_ADD); asm_store();   /* +4 = bias */
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
    /* M3D execution guard: a managed block (payload in the managed heap) must
     * not be executed as code; fail-stop before any evaluator pointer is
     * installed or any element executes. */
    e_cell(RV_T1); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j_g1 = asm_zbranch_fwd();
    e_cell(RV_T1); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_g2 = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                  /* managed block: not executable */
    asm_patch_here(j_g2);
    asm_patch_here(j_g1);
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

/* REDUCE-BLOCK: ( block -- managed-block )  evaluate each source expression
 * left-to-right, reduce it to a single value, and collect the values into a
 * freshly allocated managed block [count, site_id, v0..vN-1]. The source block
 * is a permanent loader block (never a managed block, which is data, not code),
 * so it is walked exactly like `values` walks its argument. The result block is
 * rooted on the data stack for the whole walk and its length field always
 * reflects the filled prefix, so a GC during any sub-expression marks exactly
 * the collected values. */
static void emit_reduce_block(void) {
    r_reduce_block = asm_here();
    /* input must be a BLOCK (tag 6); fail-stop otherwise */
    asm_dup(); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_not_block = asm_zbranch_fwd();
    e_untag_ptr(); e_setc(RV_T1);                    /* source block ptr */
    /* M3D execution guard: a managed block is data, not code. */
    e_cell(RV_T1); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j_g1 = asm_zbranch_fwd();
    e_cell(RV_T1); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_g2 = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();
    asm_patch_here(j_g2);
    asm_patch_here(j_g1);
    e_cell(RV_T1); asm_fetch(); e_setc(RV_T3);        /* source count (upper bound) */
    /* save evaluator state, then point it at the source block */
    e_cell(RV_CUR); asm_toR();
    e_cell(RV_END); asm_toR();
    e_cell(RV_BLK); asm_toR();
    e_cell(RV_T1); asm_lit(BLK_DATA); asm_host(HOST_ADD); e_setc(RV_CUR);
    e_cell(RV_T1); asm_lit(BLK_DATA); asm_host(HOST_ADD);
    e_cell(RV_T3); asm_host(HOST_ADD); e_setc(RV_END);
    e_cell(RV_T1); e_setc(RV_BLK);
    /* allocate the result block: 16-aligned (2 + source-count) payload cells */
    e_cell(RV_T3); asm_lit(2); asm_host(HOST_ADD); asm_lit(15); asm_host(HOST_ADD);
    asm_lit(16); asm_host(HOST_DIV); asm_lit(16); asm_host(HOST_MUL); e_setc(RV_T2);
    e_cell(RV_T2); asm_lit(GC_KIND_BLOCK); asm_call(r_alloc); e_setc(RV_T1); /* result payload */
    asm_lit(0); e_cell(RV_T1); asm_store();            /* M[result] = 0 (length) */
    asm_lit(0); e_cell(RV_T1); asm_lit(1); asm_host(HOST_ADD); asm_store(); /* site_id = 0 */
    /* root the result block on the data stack (the source block was consumed) */
    e_cell(RV_T1); asm_lit(T_BLOCK); asm_host(HOST_ADD);
    cell rloop = asm_here();
    e_cell(RV_CUR); e_cell(RV_END); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    e_cell(RV_T1); asm_toR();                          /* result payload survives subexpr */
    to_subexpr[n_subexpr++] = emit_call_fwd();         /* evaluate one expression */
    asm_fromR(); e_setc(RV_T1);                        /* restore result payload */
    asm_call(r_reduce);                                /* -> [value] */
    /* store value into result[BLK_DATA + length], then length++ */
    e_cell(RV_T1); asm_lit(BLK_DATA); asm_host(HOST_ADD);
    e_cell(RV_T1); asm_fetch(); asm_host(HOST_ADD);    /* addr = result + BLK_DATA + length */
    asm_store();
    e_cell(RV_T1); asm_fetch(); asm_lit(1); asm_host(HOST_ADD);   /* length + 1 */
    e_cell(RV_T1); asm_store();                        /* M[result] = length + 1 */
    asm_branch(rloop);
    asm_patch_here(j_done);
    asm_fromR(); e_setc(RV_BLK);
    asm_fromR(); e_setc(RV_END);
    asm_fromR(); e_setc(RV_CUR);
    asm_lit(16);                                       /* count marker: one result */
    asm_exit();                                        /* result block already on DS */
    /* input was not a BLOCK: fail-stop (the value is still on the DS) */
    asm_patch_here(j_not_block);
    asm_drop(); asm_host(HOST_DUMP); asm_halt();
}

/* RUN-BLOCK: ( block -- result-set )  evaluate a block argument as code.
 * Saves RV_CUR/RV_END/RV_BLK, points them at the block, calls block-eval,
 * restores. */
static void emit_run_block(void) {
    r_run_block = asm_here();
    e_untag_ptr(); e_setc(RV_T1);                    /* block_ptr */
    /* M3D execution guard: a managed block must not be executed as code. */
    e_cell(RV_T1); asm_lit(GC_HEAP_BASE); asm_host(HOST_GE);
    cell j_g1 = asm_zbranch_fwd();
    e_cell(RV_T1); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_g2 = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                  /* managed block: not executable */
    asm_patch_here(j_g2);
    asm_patch_here(j_g1);
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
/* SIN! natives. Every path ends in EXIT or an unwind; none falls through.
 *   create-sin type id arg  -> a new SIN! (inert data)
 *   sin? value            -> 1 if value is a SIN!, else 0
 *   sin-type / sin-id / sin-arg err -> that field
 *   raise err               -> propagate err (a non-SIN! raises 'type 'raise)
 *   judge block              -> block's result, or the SIN! it raised */
static void emit_error_natives(void) {
    /* create-sin (105): arity 3 */
    e_cell(RV_NAT); asm_lit(RN_MAKE_ERROR); asm_host(HOST_EQ);
    cell j_not_make = asm_zbranch_fwd();
    for (int k = 0; k < 3; k++) {
        to_subexpr[n_subexpr++] = emit_call_fwd();
        asm_call(r_reduce);
    }
    asm_call(r_mkerror);
    cell j_ret1 = asm_branch_fwd();                  /* -> shared [v, 1] tail */
    asm_patch_here(j_not_make);

    /* the rest (106..111) are arity 1: evaluate the argument once -> RV_T1.
     * RV_NAT is saved across it: a nested native in the argument rewrites it. */
    e_cell(RV_NAT); asm_toR();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_call(r_reduce);
    e_pop_to(RV_T1);
    asm_fromR(); e_setc(RV_NAT);

    /* sin? (106) */
    e_cell(RV_NAT); asm_lit(RN_ERRORP); asm_host(HOST_EQ);
    cell j_not_errp = asm_zbranch_fwd();
    e_cell(RV_T1); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_ERROR); asm_host(HOST_EQ);
    asm_lit(16); asm_host(HOST_MUL);                 /* 0/1 as a Glon integer */
    cell j_ret2 = asm_branch_fwd();
    asm_patch_here(j_not_errp);

    /* is the argument a SIN!? (used by the field readers and raise) */
    e_cell(RV_T1); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_ERROR); asm_host(HOST_EQ);
    e_setc(RV_T2);

    /* sin-type / sin-id / sin-arg (107..109): field offset is
     * RV_NAT - RN_MAKE_ERROR (= ERR_TYPE / ERR_ID / ERR_ARG) */
    e_cell(RV_NAT); asm_lit(RN_ERROR_ARG); asm_host(HOST_LE);
    cell j_not_field = asm_zbranch_fwd();
    e_cell(RV_T2);
    cell j_field_bad = asm_zbranch_fwd();
    e_cell(RV_T1); e_untag_ptr();
    e_cell(RV_NAT); asm_lit(RN_MAKE_ERROR); asm_host(HOST_SUB); asm_host(HOST_ADD);
    asm_fetch();
    cell j_ret3 = asm_branch_fwd();
    asm_patch_here(j_field_bad);
    emit_raise_fields("type", "sin-field", RV_T1);
    asm_patch_here(j_not_field);

    /* raise (110) */
    e_cell(RV_NAT); asm_lit(RN_RAISE); asm_host(HOST_EQ);
    cell j_not_raise = asm_zbranch_fwd();
    e_cell(RV_T2);
    cell j_raise_bad = asm_zbranch_fwd();
    e_cell(RV_T1); e_setc(RV_ERRV);
    asm_branch(r_raise_unwind);
    asm_patch_here(j_raise_bad);
    emit_raise_fields("type", "raise", RV_T1);
    asm_patch_here(j_not_raise);

    /* judge (111): the block runs in place, in the current activation and
     * context (like `do`, via RUN-BLOCK), under a transparent judge frame that
     * copies the enclosing activation's site and bias, so lexical references
     * resolve exactly as they would without the judge. RETURN unwinds through
     * the judge frame; a raise unwinds TO it. Frame layout: the 10 standard
     * fields plus FRAME_TRAPRET, 16-aligned below the caller's return address. */
    e_cell(RV_T1); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_trap_bad = asm_zbranch_fwd();
    emit_rs_guard(32);
    asm_fetchR(); e_setc(RV_SIP);                                         /* caller return */
    asm_lit(REG_RP); asm_fetch(); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_SRP);
    asm_lit(REG_RP); asm_fetch(); asm_lit(11); asm_host(HOST_SUB); asm_lit(16); asm_host(HOST_MOD); e_setc(RV_T5);
    asm_lit(REG_RP); asm_fetch(); asm_lit(11); asm_host(HOST_SUB); e_cell(RV_T5); asm_host(HOST_SUB); e_setc(RV_T6);
    e_cell(RV_T6); asm_lit(REG_RP); asm_store();                          /* RP = frame base */
    asm_lit(0); e_setc(RV_T3);                                            /* site (top level) */
    asm_lit(0); e_setc(RV_T4);                                            /* bias (top level) */
    e_cell(RV_FRAME);
    cell j_top = asm_zbranch_fwd();
    e_cell(RV_FRAME); asm_lit(FRAME_SITE); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T3);
    e_cell(RV_FRAME); asm_lit(FRAME_BIAS); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_T4);
    asm_patch_here(j_top);
    e_cell(RV_FRAME); e_cell(RV_T6); asm_lit(FRAME_PREV); asm_host(HOST_ADD); asm_store();
    e_cell(RV_T3); e_cell(RV_T6); asm_lit(FRAME_SITE); asm_host(HOST_ADD); asm_store();
    asm_lit(REG_SP); asm_fetch(); e_cell(RV_T6); asm_lit(FRAME_SP); asm_host(HOST_ADD); asm_store();
    e_cell(RV_SRP); e_cell(RV_T6); asm_lit(FRAME_RP); asm_host(HOST_ADD); asm_store();
    asm_lit(r_trap_landing); e_cell(RV_T6); asm_lit(FRAME_IP); asm_host(HOST_ADD); asm_store();
    e_cell(RV_CTX); e_cell(RV_T6); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_store();
    e_cell(RV_CUR); e_cell(RV_T6); asm_lit(FRAME_CUR); asm_host(HOST_ADD); asm_store();
    e_cell(RV_END); e_cell(RV_T6); asm_lit(FRAME_END); asm_host(HOST_ADD); asm_store();
    e_cell(RV_BLK); e_cell(RV_T6); asm_lit(FRAME_BLK); asm_host(HOST_ADD); asm_store();
    e_cell(RV_T4); e_cell(RV_T6); asm_lit(FRAME_BIAS); asm_host(HOST_ADD); asm_store();
    e_cell(RV_SIP); e_cell(RV_T6); asm_lit(FRAME_TRAPRET); asm_host(HOST_ADD); asm_store();
    e_cell(RV_T6); e_setc(RV_FRAME);
    e_cell(RV_T1);
    asm_call(r_run_block);            /* managed-block guard, save/restore CUR/END/BLK */
    /* normal completion: pop the judge frame; the block's result set stays */
    e_cell(RV_FRAME); asm_lit(FRAME_RP); asm_host(HOST_ADD); asm_fetch(); asm_lit(1); asm_host(HOST_SUB);
    asm_lit(REG_RP); asm_store();
    e_cell(RV_FRAME); asm_fetch(); e_setc(RV_FRAME);
    asm_exit();
    asm_patch_here(j_trap_bad);
    emit_raise_fields("type", "judge", RV_T1);

    /* shared tail: [value] -> [value, 1] */
    asm_patch_here(j_ret1);
    asm_patch_here(j_ret2);
    asm_patch_here(j_ret3);
    asm_lit(16);
    asm_exit();
}

static void emit_native(void) {
    r_native = asm_here();
    asm_dup(); asm_lit(16); asm_host(HOST_DIV); e_setc(RV_NAT);
    asm_drop();
#ifdef R0_S1_PROFILE
    pf_native_count();
#endif
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
    asm_branch(r_values);                          /* tail */
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
    asm_branch(r_run_block);                          /* eval then (tail) */
    asm_patch_here(j_false);
    e_cell(RV_T6);
    asm_branch(r_run_block);                          /* eval else (tail) */
    asm_patch_here(j_not_either);
    /* do (id 102): arity 1 (block) */
    e_cell(RV_NAT); asm_lit(RN_DO); asm_host(HOST_EQ);
    cell j_not_do = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_call(r_reduce);
    asm_branch(r_run_block);                          /* tail */
    asm_patch_here(j_not_do);
    /* invoke (id 103): ( closure-producing-expression -- result )  Evaluate ONE
     * expression to obtain a closure VALUE, then hand control to the closure,
     * which consumes its own arity arguments from the SAME caller expression
     * stream. This is the dynamic closure invocation (Forth EXECUTE analogue):
     * it lets a closure value obtained from select/block-at/get/return be
     * invoked exactly as if it had been reached through an ordinary word. */
    e_cell(RV_NAT); asm_lit(RN_INVOKE); asm_host(HOST_EQ);
    cell j_not_invoke = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();       /* evaluate closure-producing expr */
    asm_call(r_reduce);                              /* -> closure value */
    asm_dup(); asm_lit(16); asm_host(HOST_MOD); e_setc(RV_T4);  /* tag (value left on DS) */
    e_cell(RV_T4); asm_lit(T_CLOSURE); asm_host(HOST_EQ);
    cell j_not_closure = asm_zbranch_fwd();
    to_invoke[n_invoke++] = emit_call_fwd();         /* CALL r_invoke_closure (forward) */
    asm_exit();                                      /* return to invoke's caller */
    asm_patch_here(j_not_closure);
    asm_drop(); asm_host(HOST_DUMP); asm_halt();     /* not a closure -> fail-stop */
    asm_patch_here(j_not_invoke);
    /* reduce (id 104): arity 1 (block) -> managed block of reduced values */
    e_cell(RV_NAT); asm_lit(RN_REDUCE); asm_host(HOST_EQ);
    cell j_not_reduce = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_call(r_reduce);
    asm_branch(r_reduce_block);                      /* tail */
    asm_patch_here(j_not_reduce);
    /* SIN! natives (ids 105..111) behind ONE range check, so the arithmetic
     * fall-through below (the fib hot path) pays a single extra comparison. */
    e_cell(RV_NAT); asm_lit(RN_MAKE_ERROR); asm_host(HOST_GE);
    cell j_not_errnat = asm_zbranch_fwd();
    emit_error_natives();
    asm_patch_here(j_not_errnat);
    /* = (id 6): type-safe identity. WORD/SET/GET/LIT (tags 2..5) compare by
     * symbol id, so a quoted word and a plain word denote the same symbol (the
     * route/event dispatch relies on this). Every other tag compares by raw
     * cell identity: INT by value, NONE only with NONE, BLOCK/CLOSURE/STRING by
     * address. This removes the old numeric (int_val) "=" accidental cross-type
     * collisions -- NONE == 0, WORD == INT, block == int -- while keeping
     * word-vs-lit-word dispatch working. */
    e_cell(RV_NAT); asm_lit(RN_EQ); asm_host(HOST_EQ);
    cell j_not_eq = asm_zbranch_fwd();
    to_subexpr[n_subexpr++] = emit_call_fwd();       /* arg 1 */
    asm_call(r_reduce);
    to_subexpr[n_subexpr++] = emit_call_fwd();       /* arg 2 */
    asm_call(r_reduce);
    e_pop_to(RV_T1);                                 /* b */
    e_pop_to(RV_T2);                                 /* a */
    e_cell(RV_T1); asm_lit(16); asm_host(HOST_MOD); e_setc(RV_T3);  /* tag b */
    e_cell(RV_T2); asm_lit(16); asm_host(HOST_MOD); e_setc(RV_T4);  /* tag a */
    /* both word-family? 2 <= tag <= 5 for both a and b */
    e_cell(RV_T4); asm_lit(2); asm_host(HOST_GE);
    e_cell(RV_T4); asm_lit(5); asm_host(HOST_LE);
    asm_host(HOST_MUL);
    e_cell(RV_T3); asm_lit(2); asm_host(HOST_GE);
    e_cell(RV_T3); asm_lit(5); asm_host(HOST_LE);
    asm_host(HOST_MUL);
    asm_host(HOST_MUL);
    cell j_raw = asm_zbranch_fwd();                  /* not both word -> raw */
    e_cell(RV_T2); asm_lit(16); asm_host(HOST_DIV);  /* symbol id a */
    e_cell(RV_T1); asm_lit(16); asm_host(HOST_DIV);  /* symbol id b */
    asm_host(HOST_EQ);
    cell j_done = asm_branch_fwd();
    asm_patch_here(j_raw);
    e_cell(RV_T2); e_cell(RV_T1); asm_host(HOST_EQ);  /* a == b raw */
    asm_patch_here(j_done);
    asm_lit(16); asm_host(HOST_MUL);
    e_cell(RV_HOSTCALLS); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_HOSTCALLS);
    asm_lit(16);
    asm_exit();
    asm_patch_here(j_not_eq);
    /* arithmetic: arity 2 (numeric; = handled above, so it is excluded here) */
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
        /* FIB-OPT-P7: dispatch in fib's observed frequency order (<= then +/-
         * are the hot arithmetic natives), so the common case exits the chain
         * after one or two comparisons. Ordering only; semantics unchanged. */
        static const int op[8] = { HOST_LE, HOST_ADD, HOST_SUB, HOST_MUL, HOST_DIV,
                                   HOST_LT, HOST_GT, HOST_GE };
        static const int id[8] = { 10, 0, 1, 2, 3, 8, 9, 11 };
        cell done[8];
        for (int k = 0; k < 8; k++) {
            e_cell(RV_NAT); asm_lit(id[k]); asm_host(HOST_EQ);
            cell j = asm_zbranch_fwd();
            asm_host(op[k]);
            done[k] = asm_branch_fwd();
            asm_patch_here(j);
        }
        for (int k = 0; k < 8; k++) asm_patch_here(done[k]);
    }
#ifdef R0_S1_PROFILE
    /* trace: base-case decision at `<=`. Print 'B' + (0|1); the comparison
     * result is raw on the stack top. Skipped when PF_TRACE == 0 or when this
     * native is not `<=`. */
    {
        cell j_tr = pf_trace_begin();
        e_cell(RV_NAT); asm_lit(RN_LE); asm_host(HOST_EQ);
        cell j_not_le = asm_zbranch_fwd();
        pf_putc('B');
        e_peek(); asm_host(HOST_PRINT);
        asm_patch_here(j_not_le);
        pf_trace_end(j_tr);
    }
#endif
    asm_lit(16); asm_host(HOST_MUL);
    e_cell(RV_HOSTCALLS); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_HOSTCALLS);
    asm_lit(16);
    asm_exit();
}

/* INVOKE-CLOSURE: ( closure -- result-set )  real S1 activation */
static void emit_invoke_closure(void) {
    r_invoke_closure = asm_here();
#ifdef R0_S1_PROFILE
    pf_incr(PF_CLOSURE);                             /* one closure activation */
    pf_incr(PF_DEPTH);                               /* nest depth +1 */
    e_cell(PF_DEPTH); e_cell(PF_MAXDEPTH); asm_host(HOST_GT);
    cell j_max = asm_zbranch_fwd();
    e_cell(PF_DEPTH); e_setc(PF_MAXDEPTH);
    asm_patch_here(j_max);
#endif
    /* return-stack guard: one invocation reserves, below the guard-time RP R0,
     * exactly  1 (mkctx CALL return address) + 64 (R0S1_CTX_CELLS context)
     * + (R0-65) mod 16 (context 16-align padding, 0..15) + 16 (activation frame:
     * 10 fields + 6 padding) = at most 96 cells. The frame base therefore lands
     * at R0 - 96 in the worst case, so `RP - 96 < DS top` fail-stops (never
     * corrupts) on deep recursion. DS_INIT is the main world's data-stack
     * "empty" sentinel: push() pre-decrements, so the data stack never writes
     * cell DS_INIT itself and the return stack may legally reach it (the sentry
     * flags only rp < DS_INIT). For an M1 task the bottom is the task's
     * data-stack top (arena base + DS_OFF), detected by SP > DS_INIT (the same
     * probe the collector uses). */
    asm_lit(REG_SP); asm_fetch(); e_setc(RV_T5);     /* SP0 */
    e_cell(RV_T5); asm_lit(R0S1_DS_INIT); asm_host(HOST_GT);
    cell j_main = asm_zbranch_fwd();                  /* task? fall through */
    e_cell(RV_T5); asm_lit(M1_ARENA_BASE); asm_host(HOST_SUB);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_DIV);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_MUL);
    asm_lit(M1_ARENA_BASE); asm_host(HOST_ADD);
    asm_lit(M1_DS_OFF); asm_host(HOST_ADD); e_setc(RV_T6);   /* task DS top */
    cell j_guard = asm_branch_fwd();
    asm_patch_here(j_main);
    asm_lit(R0S1_DS_INIT); e_setc(RV_T6);             /* main DS top */
    asm_patch_here(j_guard);
    asm_lit(REG_RP); asm_fetch(); asm_lit(96); asm_host(HOST_SUB);   /* RP-96 */
    e_cell(RV_T6); asm_host(HOST_LT);                 /* RP-96 < DS top? */
    cell j_room = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                 /* return-stack exhaustion */
    asm_patch_here(j_room);
    asm_lit(0); e_setc(RV_CHILD);                    /* M2: transient roots stay 0-or-live */
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
    /* Evaluate the arity arguments.  The loop counter (RV_T4) and the loop
     * bound (RV_ARITY) are invocation-local, but they live in global cells that
     * a nested closure invocation during argument evaluation will itself
     * overwrite.  Both must therefore be saved on the return stack around each
     * argument's evaluation and restored before the loop condition is re-tested
     * -- otherwise a nested closure clobbers RV_ARITY, the outer loop sees the
     * wrong bound and terminates early, and the argument values shift. */
    asm_lit(0); e_setc(RV_T4);
    cell arg_loop = asm_here();
    e_cell(RV_T4); e_cell(RV_ARITY); asm_host(HOST_LT);
    cell j_args_done = asm_zbranch_fwd();
    e_cell(RV_T4); asm_toR();
    e_cell(RV_ARITY); asm_toR();
    to_subexpr[n_subexpr++] = emit_call_fwd();
    asm_fromR(); e_setc(RV_ARITY);
    asm_fromR(); e_setc(RV_T4);
    asm_call(r_reduce);
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_T4);
    asm_branch(arg_loop);
    asm_patch_here(j_args_done);
    asm_fromR(); e_setc(RV_ARITY);
    asm_fromR(); e_setc(RV_CLOSURE);
#ifdef R0_S1_PROFILE
    /* trace: closure entry. Print 'E' + (depth*100 + arg). Assumes arity>=1
     * (fib's only closure is arity 1); the arg is the top-of-stack at this
     * point. This block is skipped entirely when PF_TRACE == 0. */
    {
        cell j_tr = pf_trace_begin();
        pf_putc('E');
        e_peek(); asm_lit(16); asm_host(HOST_DIV);            /* arg/16 (arg on top) */
        e_cell(PF_DEPTH); asm_lit(100); asm_host(HOST_MUL);   /* depth*100 */
        asm_host(HOST_ADD);                                    /* depth*100 + arg */
        asm_host(HOST_PRINT);                                  /* leaves [arg] */
        pf_trace_end(j_tr);
    }
#endif
    /* capture return address + caller RP baseline BEFORE the context/frame are
     * allocated on the return stack (they shift RP), so RV_SIP/RV_SRP still
     * denote the closure-invocation return address. */
    asm_fetchR(); e_setc(RV_SIP);
    asm_lit(REG_RP); asm_fetch(); asm_lit(1); asm_host(HOST_ADD); e_setc(RV_SRP);
    /* child context (parent = captured) */
    e_cell(RV_CLOSURE); asm_lit(2); asm_host(HOST_ADD); asm_fetch();
    asm_call(r_mkctx);
    e_setc(RV_CHILD);
    /* The r_mkctx CALL pushed its own return target at the caller's RP top,
     * overwriting this invocation's return address (saved in RV_SIP). Restore
     * it so the normal-return epilogue's trailing EXIT pops the right address. */
    e_cell(RV_SIP); e_cell(RV_SRP); asm_lit(1); asm_host(HOST_SUB); asm_store();
    /* bind params (i = arity-1 .. 0) */
    e_cell(RV_ARITY); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_T4);
    cell bind_loop = asm_here();
    e_cell(RV_T4); asm_lit(0); asm_host(HOST_GE);
    cell j_bind_done = asm_zbranch_fwd();
    e_pop_to(RV_T6);
    e_cell(RV_CLOSURE); asm_fetch(); e_untag_ptr(); asm_lit(BLK_DATA); asm_host(HOST_ADD);
    e_cell(RV_T4); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_WORD);
    /* escape law (2): a dependent block of site S must not be bound as an
     * argument of a closure whose site is S (same-site re-entry). */
    e_cell(RV_T6); asm_lit(RV_ESC_A); asm_store();
    e_cell(RV_CLOSURE); asm_lit(CLOSURE_SITE); asm_host(HOST_ADD); asm_fetch(); asm_lit(RV_ESC_B); asm_store();
    emit_esc_ref(&r_esc_same);
    e_cell(RV_T6);
    asm_call(r_append);
    e_cell(RV_T4); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_T4);
    asm_branch(bind_loop);
    asm_patch_here(j_bind_done);
    /* Allocate the activation frame ON the return stack (task-local storage).
     * The frame pointer must be 16-aligned so its R0 tag is T_INT (0) and it is
     * never mistaken for a callable. FIB-OPT-P9: compute the 16-aligned frame
     * base once, adjust RP once, then store the 10 fields at their fixed symbolic
     * FRAME_* offsets and zero the padding -- instead of ten derived >R pushes
     * (each re-reads and re-writes the memory-mapped RP). The physical layout
     * and RAW-visible offsets are unchanged. */
    asm_lit(REG_RP); asm_fetch(); asm_lit(10); asm_host(HOST_SUB); asm_lit(16); asm_host(HOST_MOD); e_setc(RV_T5);  /* padding */
    asm_lit(REG_RP); asm_fetch(); asm_lit(10); asm_host(HOST_SUB); e_cell(RV_T5); asm_host(HOST_SUB); e_setc(RV_T6);  /* frame base */
    e_cell(RV_T6); asm_lit(REG_RP); asm_store();          /* RP = frame base (one adjustment) */
    e_cell(RV_FRAME); e_cell(RV_T6); asm_lit(FRAME_PREV); asm_host(HOST_ADD); asm_store();   /* +0 = prev (old frame) */
    e_cell(RV_T6); e_setc(RV_FRAME);                       /* RV_FRAME = frame base (16-aligned) */
    e_cell(RV_CLOSURE); asm_lit(CLOSURE_SITE); asm_host(HOST_ADD); asm_fetch();
        e_cell(RV_T6); asm_lit(FRAME_SITE); asm_host(HOST_ADD); asm_store();               /* +1 = site */
    asm_lit(REG_SP); asm_fetch(); e_cell(RV_T6); asm_lit(FRAME_SP); asm_host(HOST_ADD); asm_store();   /* +2 = SP */
    e_cell(RV_SRP); e_cell(RV_T6); asm_lit(FRAME_RP); asm_host(HOST_ADD); asm_store();     /* +3 = RP */
    e_cell(RV_SIP); e_cell(RV_T6); asm_lit(FRAME_IP); asm_host(HOST_ADD); asm_store();     /* +4 = IP */
    e_cell(RV_CTX); e_cell(RV_T6); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_store();    /* +5 = CTX */
    e_cell(RV_CUR); e_cell(RV_T6); asm_lit(FRAME_CUR); asm_host(HOST_ADD); asm_store();    /* +6 = CUR */
    e_cell(RV_END); e_cell(RV_T6); asm_lit(FRAME_END); asm_host(HOST_ADD); asm_store();    /* +7 = END */
    e_cell(RV_BLK); e_cell(RV_T6); asm_lit(FRAME_BLK); asm_host(HOST_ADD); asm_store();    /* +8 = BLK */
    e_cell(RV_CLOSURE); asm_lit(CLOSURE_BIAS); asm_host(HOST_ADD); asm_fetch();
        e_cell(RV_T6); asm_lit(FRAME_BIAS); asm_host(HOST_ADD); asm_store();              /* +9 = BIAS */
    /* zero the padding cells (frame+10 .. frame+9+padding) so the GC never
     * mistakes them for closures. */
    cell pad_loop = asm_here();
    e_cell(RV_T5); asm_lit(0); asm_host(HOST_GT);
    cell j_pad_done = asm_zbranch_fwd();
    e_cell(RV_T5); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_T5);
    asm_lit(0);
    e_cell(RV_T6); asm_lit(10); asm_host(HOST_ADD); e_cell(RV_T5); asm_host(HOST_ADD);
    asm_store();
    asm_branch(pad_loop);
    asm_patch_here(j_pad_done);
    /* enter body */
    e_cell(RV_CHILD); e_setc(RV_CTX);
    e_cell(RV_CLOSURE); asm_lit(CLOSURE_BODY); asm_host(HOST_ADD); asm_fetch(); e_untag_ptr(); e_setc(RV_BODY);
    e_cell(RV_BODY); asm_lit(BLK_DATA); asm_host(HOST_ADD); e_setc(RV_CUR);
    e_cell(RV_BODY); asm_lit(BLK_DATA); asm_host(HOST_ADD);
    e_cell(RV_BODY); asm_fetch(); asm_host(HOST_ADD); e_setc(RV_END);
    e_cell(RV_BODY); e_setc(RV_BLK);
    to_block_eval[n_block_eval++] = emit_call_fwd();
    /* escape law (1): a frame must not return an activation-dependent block */
    emit_esc_ref(&r_esc_results);
    /* normal return: restore caller state from frame, pop frame */
    e_cell(RV_FRAME); asm_lit(FRAME_END); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_END);
    e_cell(RV_FRAME); asm_lit(FRAME_CUR); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_CUR);
    e_cell(RV_FRAME); asm_lit(FRAME_BLK); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_BLK);
    e_cell(RV_FRAME); asm_lit(FRAME_CTX); asm_host(HOST_ADD); asm_fetch(); e_setc(RV_CTX);
    /* release the frame (10 fields + 16-align padding): RP = frame.RP - 1, the
     * invocation's return-stack baseline (equivalent to the old
     * RP += (frame.RP - 1 - frame) because RP == frame base here). The trailing
     * asm_exit then pops the return address at that position. */
    e_cell(RV_FRAME); asm_lit(FRAME_RP); asm_host(HOST_ADD); asm_fetch(); asm_lit(1); asm_host(HOST_SUB);
    asm_lit(REG_RP); asm_store();
    e_cell(RV_FRAME); asm_fetch(); e_setc(RV_FRAME);
    asm_lit(0); e_setc(RV_CHILD);                    /* M2: child context now dead */
    asm_lit(0); e_setc(RV_CLOSURE);                  /* M2: closure now dead */
#ifdef R0_S1_PROFILE
    /* trace: closure exit. Print 'R' + result. The result set is [result,
     * tagged-N] on the data stack, so the result is at M[SP+1]. Skipped when
     * PF_TRACE == 0. */
    {
        cell j_tr = pf_trace_begin();
        pf_putc('R');
        asm_lit(REG_SP); asm_fetch(); asm_lit(1); asm_host(HOST_ADD); asm_fetch();
        asm_lit(16); asm_host(HOST_DIV);
        asm_host(HOST_PRINT);
        pf_trace_end(j_tr);
    }
    e_cell(PF_DEPTH); asm_lit(1); asm_host(HOST_SUB); e_setc(PF_DEPTH);  /* nest depth -1 */
#endif
    asm_exit();
}

/* RETURN: ( -- ) non-local definitional return. Reads the current block's
 * return-site-id, evaluates the return argument, finds the live activation
 * with that site-id, and restores its saved SP/RP/IP/RV state, jumping to its
 * return address. Intermediate activations' epilogues never run. */
static void emit_return(void) {
    r_return = asm_here();
    /* site_id = M[RV_BLK + BLK_SITE] */
    e_cell(RV_BLK); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch(); emit_decode_site(); e_setc(RV_SITE);
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
    /* a JUDGE frame copies its enclosing activation's site but is not an
     * activation: RETURN unwinds through it to the real function frame. */
    e_cell(RV_T2); asm_lit(FRAME_IP); asm_host(HOST_ADD); asm_fetch();
    asm_lit(r_trap_landing); asm_host(HOST_NE);
    cell j_trapframe = asm_zbranch_fwd();
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
    asm_lit(0); e_setc(RV_CHILD);                    /* M2: abandoned invocation's child dead */
    asm_lit(0); e_setc(RV_CLOSURE);                  /* M2: abandoned closure dead */
    e_cell(RV_T2); asm_lit(FRAME_IP); asm_host(HOST_ADD); asm_fetch(); asm_lit(REG_IP); asm_store();
    /* (control is now at the target's return address) */
    asm_patch_here(j_nomatch);
    asm_patch_here(j_trapframe);
    e_cell(RV_T2); asm_fetch(); e_setc(RV_T2);         /* frame = prev */
    asm_branch(find_loop);
}

/* INVOKE-RAW: ( raw -- result-set )  call a first-class RAW S1 fragment.
 * Evaluates `arity` ordinary R0 arguments (each reduced to one value), CALLs
 * the fragment's entry, and returns whatever result set the fragment leaves.
 * The fragment itself is trusted/unsafe S1 code. */
static void emit_invoke_raw(void) {
    r_invoke_raw = asm_here();
#ifdef R0_S1_PROFILE
    pf_incr(PF_RAW);
#endif
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
    cell fw_disp_word, fw_disp_bound;   /* forward branches to the shared value dispatch */
#ifdef R0_S1_PROFILE
    pf_incr(PF_SUBEXPR);
#endif
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
    /* Literal-body candidate: the parser opens a fresh lexical scope/site only
     * for a literal block written directly as the body (`func SPEC [...]`), so
     * remember the element at the body position (a loader block, else 0) and
     * compare it with the evaluated body below. Kept on the DS (a valid value). */
    asm_lit(0);
    e_cell(RV_CUR); asm_lit(1); asm_host(HOST_ADD); e_cell(RV_END); asm_host(HOST_LT);
    cell j_nocand = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_CUR); asm_lit(1); asm_host(HOST_ADD); asm_fetch();
    asm_dup(); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_candblk = asm_zbranch_fwd();
    cell j_candok = asm_branch_fwd();
    asm_patch_here(j_candblk);
    asm_drop(); asm_lit(0);
    asm_patch_here(j_candok);
    asm_patch_here(j_nocand);
    to_subexpr[n_subexpr++] = emit_call_fwd();  /* spec */
    asm_call(r_reduce);
    to_subexpr[n_subexpr++] = emit_call_fwd();  /* body */
    asm_call(r_reduce);
    e_pop_to(RV_T2);                             /* body */
    e_pop_to(RV_T3);                             /* spec */
    e_pop_to(RV_T1);                             /* literal-body candidate */
    /* a func body must be a block: anything else would be read as a block
     * header below and executed as garbage, so fail-stop instead. */
    e_cell(RV_T2); asm_lit(16); asm_host(HOST_MOD); asm_lit(T_BLOCK); asm_host(HOST_EQ);
    cell j_bodyblk = asm_zbranch_fwd();
    cell j_bodyok = asm_branch_fwd();
    asm_patch_here(j_bodyblk);
    asm_host(HOST_DUMP); asm_halt();
    asm_patch_here(j_bodyok);
    /* Guard of Binding. A literal body is bound to the new func's own fresh
     * scope: capture RV_CTX, bias 0. A computed body (e.g. lambda's `:body`, or
     * a block taken from data) keeps the lexical meaning it was parsed with:
     *   - site 0 (a top-level block): its origin is the global context;
     *   - live origin (see BINDING-CTX): capture it with a +1 depth bias;
     *   - dead origin: capture the global context and mark the closure's site
     *     DEAD_SITE, which matches no block, so any T_BOUND reference in the
     *     body fail-stops at the LOAD-LEX guard instead of reading an
     *     unrelated context's slot. */
    e_cell(RV_T2); e_untag_ptr(); asm_lit(BLK_SITE); asm_host(HOST_ADD); asm_fetch(); emit_decode_site(); e_setc(RV_SITE);  /* site = body.BLK_SITE */
    e_cell(RV_T2); e_cell(RV_T1); asm_host(HOST_EQ);
    cell j_computed = asm_zbranch_fwd();
    e_cell(RV_CTX); e_setc(RV_T5);               /* literal: captured = RV_CTX */
    asm_lit(0); e_setc(RV_T4);                   /* bias = 0 */
    cell j_gotbind = asm_branch_fwd();
    asm_patch_here(j_computed);
    e_cell(RV_SITE); asm_lit(0); asm_host(HOST_EQ);
    cell j_sited = asm_zbranch_fwd();
    e_cell(GC_GLOBAL_CTX); e_setc(RV_T5);        /* site 0: captured = global */
    asm_lit(0); e_setc(RV_T4);
    cell j_gotbind2 = asm_branch_fwd();
    asm_patch_here(j_sited);
    e_cell(RV_SITE); asm_call(r_binding_ctx);    /* -> [origin 1] | [0 0] */
    e_pop_to(RV_T4);                             /* bias */
    e_pop_to(RV_T5);                             /* origin ctx */
    e_cell(RV_T4); asm_lit(0); asm_host(HOST_EQ);
    cell j_gotbind3 = asm_zbranch_fwd();
    e_cell(GC_GLOBAL_CTX); e_setc(RV_T5);        /* dead origin: captured = global */
    asm_lit(DEAD_SITE); e_setc(RV_SITE);         /* ... and no block matches it */
    asm_patch_here(j_gotbind);
    asm_patch_here(j_gotbind2);
    asm_patch_here(j_gotbind3);
    e_cell(RV_T3);                               /* spec */
    e_cell(RV_T2);                               /* body */
    e_cell(RV_T5);                               /* captured */
    e_cell(RV_SITE);                             /* site-id */
    e_cell(RV_T4);                               /* bias */
    asm_call(r_mkclosure);
    asm_lit(16);
    asm_exit();
    asm_patch_here(j_notfunc);
    /* return keyword? word == mk_word(1) */
    asm_dup(); asm_lit(mk_word(RETURN_SYM)); asm_host(HOST_EQ);
    cell j_notreturn = asm_zbranch_fwd();
    asm_drop();
    asm_branch(r_return);                        /* non-local return (jumps away) */
    asm_patch_here(j_notreturn);
    asm_call(r_lookup);
    asm_dup(); asm_lit(-1); asm_host(HOST_EQ);
    cell j_err = asm_zbranch_fwd();
    asm_drop(); asm_host(HOST_DUMP); asm_halt();
    asm_patch_here(j_err);
    fw_disp_word = asm_branch_fwd();             /* -> shared value dispatch */
    asm_patch_here(j2);

    /* BOUND (tag 13): pre-resolved lexical (depth, slot) reference. Checked
     * immediately after WORD: parameter references are as common as words in
     * the hot path, so testing this before the rare SET/GET/LIT tags saves
     * several failed comparisons per reference. */
    asm_dup(); asm_lit(T_BOUND); asm_host(HOST_EQ);
    cell j_bound = asm_zbranch_fwd();
    asm_drop();
    asm_call(r_load_lex);                        /* [value] */
    fw_disp_bound = asm_branch_fwd();            /* -> shared value dispatch */
    asm_patch_here(j_bound);

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

    /* shared value dispatch: [value] -> [result, 16] (word + bound refs) */
    asm_patch_here(fw_disp_word);
    asm_patch_here(fw_disp_bound);
    asm_dup(); asm_lit(16); asm_host(HOST_MOD);
    /* native (tag 9) */
    asm_dup(); asm_lit(T_NATIVE); asm_host(HOST_EQ);
    cell j_notnat = asm_zbranch_fwd();
    asm_drop();
    asm_branch(r_native);          /* tail call: native's EXIT returns to our caller */
    asm_patch_here(j_notnat);
    /* closure (tag 8) */
    asm_dup(); asm_lit(T_CLOSURE); asm_host(HOST_EQ);
    cell j_notclosure = asm_zbranch_fwd();
    asm_drop();
    asm_branch(r_invoke_closure);  /* tail call */
    asm_patch_here(j_notclosure);
    /* raw (tag 10) */
    asm_dup(); asm_lit(T_RAW); asm_host(HOST_EQ);
    cell j_notraw = asm_zbranch_fwd();
    asm_drop();
    asm_branch(r_invoke_raw);      /* tail call */
    asm_patch_here(j_notraw);
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
#ifdef R0_S1_PROFILE
    pf_incr(PF_BLKEVAL);         /* one block-evaluator iteration */
#endif
    asm_call(r_subexpr);
    e_cell(RV_CUR); e_cell(RV_END); asm_host(HOST_LT);
    cell j_done = asm_zbranch_fwd();
    /* discard the non-final result set inline (denser: no CALL+EXIT per element) */
    asm_lit(16); asm_host(HOST_DIV);
    e_setc(RV_N);
    cell dloop = asm_here();
    e_cell(RV_N); asm_lit(0); asm_host(HOST_GT);
    cell jd = asm_zbranch_fwd();
    asm_drop();
    e_cell(RV_N); asm_lit(1); asm_host(HOST_SUB); e_setc(RV_N);
    asm_branch(dloop);
    asm_patch_here(jd);
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
    main_halt_ip = asm_here() + 1;      /* OP_HALT at asm_here(); IP lands here+1 */
    asm_halt();
}

/* ============================ public API =============================== */

cell r0_s1_init(void) {
    M[GC_LOADER_HP] = R0S1_HEAP_BASE;
    nsyms = 0;
    n_subexpr = 0; n_block_eval = 0;
    n_invoke = 0;
    n_fw_mv = 0; n_fw_mf = 0;
    n_esc_refs = 0;
    n_word_refs = 0;
    next_site = 1;      /* func-site-ids start at 1; 0 = "no enclosing func" */
    site_depth = 0;
    parse_func_depth = 0;
    last_block_dep = 0;
    M[SITE_PARENT_BASE] = 0;

    asm_reset();
    code_begin = asm_here();
    emit_gc();                      /* M2: mark/sweep collector + allocator */
    emit_error_core();              /* SIN!: mkerror, judge landing, raise-unwind */
    emit_reduce();
    emit_lookup();
    emit_load_lex();
    emit_hash_insert();
    emit_build_hash();
    emit_set();
    emit_append();
    emit_mkctx();
    emit_mkclosure();
    emit_binding_ctx();
    emit_values();
    emit_reduce_block();
    emit_run_block();
    emit_native();
    emit_invoke_closure();
    emit_return();
    emit_invoke_raw();
    emit_subexpr();
    emit_block_eval();
    emit_main();
    emit_esc_any();
    emit_esc_transport();
    emit_esc_eq();
    emit_esc_same();
    emit_esc_anc();
    emit_esc_results();
    emit_esc_capture();
    for (int i = 0; i < n_esc_refs; i++)
        s1_set_mem(esc_refs[i].patch, *esc_refs[i].slot);
    code_end = asm_here();

    for (int i = 0; i < n_subexpr; i++) s1_set_mem(to_subexpr[i], r_subexpr);
    for (int i = 0; i < n_block_eval; i++) s1_set_mem(to_block_eval[i], r_block_eval);
    for (int i = 0; i < n_invoke; i++) s1_set_mem(to_invoke[i], r_invoke_closure);

    /* preload the global environment ("func"/"return"/"raw" first => sym 0/1/2) */
    intern("func");
    intern("return");
    intern("raw");
    /* G1E + STRING! integration: the global context holds every top-level
     * definition (natives, view vocabulary atoms, application state/views/
     * routes, and the hoisted str-N string bindings). The regenerated shop
     * bundle has 129 top-level set-words + 13 natives = 142 bindings, so the
     * cap must exceed 142. */
    global_ctx = make_context(R0_NONE, 256);
    if (global_ctx == R0_NONE) {             /* a fresh loader heap always fits it */
        fprintf(stderr, "r0_s1: no room for the global context\n");
        abort();
    }
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
    bind(global_ctx, intern("invoke"), mk_native(RN_INVOKE));
    bind(global_ctx, intern("reduce"), mk_native(RN_REDUCE));
    /* SIN! vocabulary */
    bind(global_ctx, intern("create-sin"), mk_native(RN_MAKE_ERROR));
    bind(global_ctx, intern("sin?"), mk_native(RN_ERRORP));
    bind(global_ctx, intern("sin-type"), mk_native(RN_ERROR_TYPE));
    bind(global_ctx, intern("sin-id"), mk_native(RN_ERROR_ID));
    bind(global_ctx, intern("sin-arg"), mk_native(RN_ERROR_ARG));
    bind(global_ctx, intern("raise"), mk_native(RN_RAISE));
    bind(global_ctx, intern("judge"), mk_native(RN_TRAP));
    /* word literals in emitted code (emitted before the preload above) */
    for (int i = 0; i < n_word_refs; i++)
        s1_set_mem(word_refs[i].patch, intern(word_refs[i].name));

    /* M2: seed GC roots and world state.  The collector scans the global
     * context, the (empty) M1 task table, and the (empty) scheduler-world DS/RS
     * unconditionally, so seed every slot EMPTY and the scheduler stacks empty
     * so plain (non-multitasking) runs scan to nothing. */
    M[GC_GLOBAL_CTX] = global_ctx;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    M[M1_SCHED_REC + 1] = R0S1_DS_INIT;   /* scheduler DS: empty range */
    M[M1_SCHED_REC + 2] = R0S1_RS_INIT;   /* scheduler RS: empty range */
    M[GC_COLLECT_CNT] = 0;
    M[GC_LAST_RECLAM] = 0;
    /* Seed the managed-heap frontier. The non-persistent r0_s1_run path resets
     * REG_HP via s1_reset(), but the G1E load-on-demand path calls
     * r0_s1_run_persistent FIRST (bootstrap), which preserves REG_HP as-is; it
     * must therefore start at the empty-heap base, not an uninitialised cell. */
    M[REG_HP] = GC_HEAP_BASE;

    /* M3: the BUILTIN_TYPE table and GC_META cell are always scanned as roots.
     * Seed them NONE so non-M3 runs scan to nothing; r0_s1_seed_datatypes()
     * fills them with the canonical descriptors for M3 runs. */
    g_seed_datatypes = 0;
    M[GC_META] = R0_NONE;
    for (int i = 0; i < 16; i++) M[BUILTIN_BASE + i] = R0_NONE;

    return main_entry;
}

/* M3: bind the datatype! meta-descriptor word and the 11 built-in type words
 * into the global context, and enable heap seeding on the next run. The
 * descriptors live at fixed addresses at the bottom of the managed heap; the
 * heap itself is seeded by seed_datatype_heap() after s1_reset(). */
void r0_s1_seed_datatypes(void) {
    static const char *tn[11] = {
        "integer!", "none!", "word!", "set-word!", "get-word!", "lit-word!",
        "block!", "context!", "closure!", "native!", "raw!"
    };
    g_seed_datatypes = 1;
    bind(global_ctx, intern("datatype!"), mk_user(GC_META_PAYLOAD));
    for (int t = 0; t < 11; t++)
        bind(global_ctx, intern(tn[t]), mk_user(GC_META_PAYLOAD + 32 * (t + 1)));
    /* M3C: STRING! is an intrinsic tag (12) whose canonical descriptor lives in
     * BUILTIN_TYPE slot 12 (seeded by seed_datatype_heap). */
    bind(global_ctx, intern("string!"), mk_user(BUILTIN_STRING_PAYLOAD));
}

/* M3: seed the 12 bootstrap descriptors (meta + 11 built-ins) at the bottom of
 * the managed heap, fill BUILTIN_TYPE and GC_META, and advance REG_HP past
 * them. Called from r0_s1_run after s1_reset() so REG_HP is correct. */
static void seed_datatype_heap(void) {
    cell base = GC_HEAP_BASE;
    cell meta = GC_META_PAYLOAD;
    M[base] = 32;                                   /* header size */
    M[base + 1] = GC_FLAG_ALLOC | (GC_KIND_USER << 2);
    M[meta] = mk_user(meta);                        /* desc = self */
    M[meta + 1] = 0;                                /* count = 0 */
    for (int t = 0; t < 11; t++) {
        cell b = base + 32 + 32 * t;
        cell p = b + 16;
        M[b] = 32;
        M[b + 1] = GC_FLAG_ALLOC | (GC_KIND_USER << 2);
        M[p] = mk_user(meta);                       /* desc = datatype! */
        M[p + 1] = 0;                               /* count = 0 */
        M[BUILTIN_BASE + t] = mk_user(p);
    }
    for (int t = 11; t < 16; t++) M[BUILTIN_BASE + t] = R0_NONE;
    /* M3C: STRING! built-in descriptor (BUILTIN_TYPE slot 12). Slot 11 (T_USER)
     * stays NONE because type? special-cases tag 11 to read the object's own
     * descriptor. The string! descriptor is seeded contiguously after the 11
     * built-ins at header 33152 / payload 33168. */
    {
        cell b = base + 12 * 32;
        cell p = b + 16;
        M[b] = 32;
        M[b + 1] = GC_FLAG_ALLOC | (GC_KIND_USER << 2);
        M[p] = mk_user(meta);                       /* desc = datatype! */
        M[p + 1] = 0;                               /* count = 0 */
        M[BUILTIN_BASE + 12] = mk_user(p);
    }
    M[GC_META] = mk_user(meta);
    M[REG_HP] = base + 13 * 32;
}

static cell parse_program(parser_t *P) {
    skip_ws(P);
    if (P->s[P->pos] == '[') return parse_block(P, 0);
    cell tmp[512]; int n = 0;
    while (P->s[P->pos] && !P->err && !parse_fail) {
        skip_ws(P);
        if (!P->s[P->pos]) break;
        if (P->s[P->pos] == '"') {
            tmp[n++] = intern("mk-string");
            tmp[n++] = parse_string_literal(P);
            if (n >= 512) { parse_fail_set(R0S1_PARSE_TOO_LARGE); P->err = 1; break; }
            continue;
        }
        tmp[n++] = parse_form(P);
        if (n >= 512) { parse_fail_set(R0S1_PARSE_TOO_LARGE); P->err = 1; break; }
    }
    cell b = make_block((cell)n);
    if (b == R0_NONE) { P->err = 1; return R0_NONE; }
    cell p = r0_untag(b);
    M[p] = (cell)n;
    M[p + BLK_SITE] = 0;
    for (int i = 0; i < n; i++) M[p + BLK_DATA + i] = tmp[i];
    return b;
}

/* Parse src into a loader block. On failure *err is set, r0_s1_parse_error_kind()
 * says why, and the parse is rolled back completely: the loader heap, the
 * func-site counter and the symbol table are restored to their state at entry,
 * so a failed parse consumes nothing and earlier session state is untouched.
 * The return value is then R0_NONE and must not be run. (Emitted code from a
 * `masm` form inside the failed source is the one thing not reclaimed.) */
cell r0_s1_parse(const char *src, int *err) {
    parser_t P; P.s = src; P.pos = 0; P.err = 0;
    cell save_lhp = M[GC_LOADER_HP];
    int save_site = next_site;
    int save_nsyms = nsyms;
    *err = 0;
    parse_fail = R0S1_PARSE_OK;
    site_depth = 0;
    lex_depth = 0;
    parse_func_depth = 0;
    cell b = parse_program(&P);
    if (!P.err && !parse_fail) return b;
    if (!parse_fail) parse_fail = R0S1_PARSE_SYNTAX;
    M[GC_LOADER_HP] = save_lhp;
    next_site = save_site;
    while (nsyms > save_nsyms) free((void *)syms[--nsyms]);
    site_depth = 0;
    lex_depth = 0;
    parse_func_depth = 0;
    last_block_dep = 0;
    *err = 1;
    return R0_NONE;
}

int r0_s1_parse_error_kind(void) { return parse_fail; }

/* STACK-SENTRY: after a run the SP/RP must be back inside the legal region of
 * the world the run ended in (main DS [code_end, DS_INIT], main RS
 * [DS_INIT, RS_INIT], or a task DS [base, base+M1_DS_OFF] / task RS
 * [base+M1_DS_OFF, base+M1_RS_OFF]). A violation is reported immediately, with
 * the offending register state, not a later GC or unrelated failure. */
static void r0_s1_stack_sentry(void) {
    cell sp = s1_mem(REG_SP), rp = s1_mem(REG_RP);
    const char *what = NULL;
    if (sp <= R0S1_DS_INIT) {
        /* main world */
        if (sp < code_end)              what = "main data-stack overflow (SP < code)";
        else if (rp > R0S1_RS_INIT)     what = "main return-stack underflow (RP > RS_INIT)";
        else if (rp < R0S1_DS_INIT)     what = "main return-stack overflow (RP < DS_INIT)";
    } else if (sp >= M1_ARENA_BASE &&
               sp < M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS) {
        /* M1 task world (slot derived from SP; SP is always >= its arena base) */
        cell base = M1_ARENA_BASE + ((sp - M1_ARENA_BASE) / M1_TASK_CELLS) * M1_TASK_CELLS;
        cell ds_top = base + M1_DS_OFF;
        cell rs_top = base + M1_RS_OFF;
        if (sp > ds_top)                what = "task data-stack underflow (SP > DS top)";
        else if (rp > rs_top)           what = "task return-stack underflow (RP > RS top)";
        else if (rp < ds_top)           what = "task return-stack overflow (RP < DS top)";
    } else {
        what = "invalid SP (outside main and task regions)";
    }
    if (what) {
        stack_sentry_fired = 1;
        fprintf(stderr, "stack sentry: %s (SP=%ld RP=%ld)\n",
                what, (long)sp, (long)rp);
    }
}

int r0_s1_run_ex(cell block, int preserve_hp) {
    cell bp = r0_untag(block);
    M[RV_CUR] = bp + BLK_DATA;
    M[RV_END] = bp + BLK_DATA + M[bp];
    M[RV_BLK] = bp;
    M[RV_CTX] = global_ctx;
    M[RV_FRAME] = 0;
    M[RV_CHILD] = 0;          /* M2: keep transient roots 0-or-live */
    M[RV_CLOSURE] = 0;
    M[RV_HOSTCALLS] = 0;
    M[RV_RPMIN] = 65535;
    M[RV_SPMIN] = 65535;
    stack_sentry_fired = 0;
    M[RV_ERRUNC] = 0;          /* SIN!: no pending or uncaught error */
    M[RV_HALTWHY] = R0S1_HALT_NONE;
    M[RV_ERRV] = R0_NONE; M[RV_ERRT] = R0_NONE; M[RV_ERRI] = R0_NONE; M[RV_ERRA] = R0_NONE;
#ifdef R0_S1_PROFILE
    for (cell c = PF_BASE; c <= PF_HASH_FALLBACK_SLOTS; c++)
        if (c != PF_TRACE) M[c] = 0;   /* PF_TRACE is a persistent mode flag */
#endif

    /* G1 persistent machine: reset transient stacks/registers but PRESERVE the
     * managed-heap frontier (REG_HP) so load-time and earlier-interaction
     * managed values survive across route/event calls instead of being
     * overwritten by s1_reset()'s heap re-base. */
    cell saved_hp = 0;
    if (preserve_hp) saved_hp = s1_mem(REG_HP);
    s1_reset();
    if (preserve_hp) s1_set_mem(REG_HP, saved_hp);
    if (g_seed_datatypes) seed_datatype_heap();
    ip_start = s1_mem(REG_IP);
    sp_start = s1_mem(REG_SP);
    rp_start = s1_mem(REG_RP);
    s1_run(main_entry);
    ip_end = s1_mem(REG_IP);
    sp_end = s1_mem(REG_SP);
    rp_end = s1_mem(REG_RP);
    r0_s1_stack_sentry();

    cell arity = s1_top();
    return (int)(arity / 16);
}

int r0_s1_run(cell block) {
    return r0_s1_run_ex(block, 0);
}

int r0_s1_run_persistent(cell block) {
    return r0_s1_run_ex(block, 1);
}

/* FIB-OPT-P10A: identical setup/result-reading to r0_s1_run, but execute the
 * already-assembled S1 stream via the caller's compiled executor `run_fn`
 * (which receives the shared M[] and the EVAL_LOOP entry) instead of the
 * interpreted s1_run(). */
int r0_s1_run_compiled(cell block, void (*run_fn)(cell *, cell)) {
    cell bp = r0_untag(block);
    M[RV_CUR] = bp + BLK_DATA;
    M[RV_END] = bp + BLK_DATA + M[bp];
    M[RV_BLK] = bp;
    M[RV_CTX] = global_ctx;
    M[RV_FRAME] = 0;
    M[RV_CHILD] = 0;
    M[RV_CLOSURE] = 0;
    M[RV_HOSTCALLS] = 0;
    M[RV_RPMIN] = 65535;
    M[RV_SPMIN] = 65535;
    stack_sentry_fired = 0;
    M[RV_ERRUNC] = 0;          /* SIN!: no pending or uncaught error */
    M[RV_HALTWHY] = R0S1_HALT_NONE;
    M[RV_ERRV] = R0_NONE; M[RV_ERRT] = R0_NONE; M[RV_ERRI] = R0_NONE; M[RV_ERRA] = R0_NONE;
#ifdef R0_S1_PROFILE
    for (cell c = PF_BASE; c <= PF_HASH_FALLBACK_SLOTS; c++)
        if (c != PF_TRACE) M[c] = 0;
#endif

    s1_reset();
    if (g_seed_datatypes) seed_datatype_heap();
    ip_start = s1_mem(REG_IP);
    sp_start = s1_mem(REG_SP);
    rp_start = s1_mem(REG_RP);
    run_fn(M, main_entry);
    ip_end = s1_mem(REG_IP);
    sp_end = s1_mem(REG_SP);
    rp_end = s1_mem(REG_RP);
    r0_s1_stack_sentry();

    cell arity = s1_top();
    return (int)(arity / 16);
}

cell r0_s1_result(int i, int N) {
    cell sp = s1_mem(REG_SP);
    return s1_mem(sp + N - i);
}

cell r0_s1_ip_start(void) { return ip_start; }
cell r0_s1_ip_end(void)   { return ip_end; }
int  r0_s1_ran_cleanly(void){ return ip_end == main_halt_ip; }
int  r0_s1_uncaught_error(cell *type, cell *id, cell *arg) {
    if (r0_s1_ran_cleanly() || M[RV_ERRUNC] != 1) return 0;
    cell t = M[RV_ERRT], i = M[RV_ERRI], a = M[RV_ERRA], v = M[RV_ERRV];
    if ((v & 15) == T_ERROR) {
        cell p = v - T_ERROR;
        t = M[p + ERR_TYPE]; i = M[p + ERR_ID]; a = M[p + ERR_ARG];
    }
    if (type) *type = t;
    if (id) *id = i;
    if (arg) *arg = a;
    return 1;
}
const char *r0_s1_sym_name(cell id) {
    return (id >= 0 && id < nsyms) ? syms[id] : 0;
}
int  r0_s1_stack_sentry_fired(void){ return stack_sentry_fired; }
void r0_s1_global_usage(cell *count, cell *cap) {
    cell p = r0_untag(global_ctx);
    if (count) *count = M[p + CTX_COUNT];
    if (cap) *cap = M[p + CTX_CAP];
}
int  r0_s1_halt_reason(void) {
    return r0_s1_ran_cleanly() ? R0S1_HALT_NONE : (int)M[RV_HALTWHY];
}
cell r0_s1_sp_start(void) { return sp_start; }
cell r0_s1_sp_end(void)   { return sp_end; }
cell r0_s1_rp_start(void) { return rp_start; }
cell r0_s1_rp_end(void)   { return rp_end; }
cell r0_s1_rp_min(void)   { return M[RV_RPMIN]; }
cell r0_s1_sp_min(void)   { return M[RV_SPMIN]; }
cell r0_s1_host_calls(void){ return M[RV_HOSTCALLS]; }
cell r0_s1_code_size(void) { return code_end - code_begin; }

/* M2 GC diagnostics (test/audit only) */
cell r0_s1_gc_collect_addr(void) { return r_collect; }
cell r0_s1_alloc_addr(void)       { return r_alloc; }
cell r0_s1_lookup_addr(void)      { return r_lookup; }
long r0_s1_gc_count(void)        { return (long)M[GC_COLLECT_CNT]; }
long r0_s1_gc_live_cells(void)   { return (long)M[GC_LIVE_CELLS]; }
long r0_s1_gc_live_objs(void)    { return (long)M[GC_LIVE_OBJS]; }
long r0_s1_gc_free_cells(void)   { return (long)M[GC_FREE_CELLS]; }
long r0_s1_gc_free_blocks(void)  { return (long)M[GC_FREE_BLOCKS]; }
long r0_s1_gc_reclaimed(void)    { return (long)M[GC_LAST_RECLAM]; }
long r0_s1_heap_high(void)       { return (long)s1_mem(REG_HP); }

/* FIB-PROFILE-P1: read the profiler counters into a struct. */
void r0_s1_pf_read(r0_s1_pf_stats *out) {
    out->closure    = (long)M[PF_CLOSURE];
    out->subexpr    = (long)M[PF_SUBEXPR];
    out->blkeval    = (long)M[PF_BLKEVAL];
    out->lookup     = (long)M[PF_LOOKUP];
    out->lk_slots   = (long)M[PF_LK_SLOTS];
    out->lk_parent  = (long)M[PF_LK_PARENT];
    out->native     = (long)M[PF_NATIVE];
    out->nat_le     = (long)M[PF_NAT_LE];
    out->nat_sub    = (long)M[PF_NAT_SUB];
    out->nat_add    = (long)M[PF_NAT_ADD];
    out->nat_either = (long)M[PF_NAT_EITHER];
    out->nat_other  = (long)M[PF_NAT_OTHER];
    out->allocs     = (long)M[PF_ALLOCS];
    out->alloc_cells= (long)M[PF_ALLOC_CELLS];
    out->alloc_ctx  = (long)M[PF_ALLOC_CTX];
    out->alloc_frame= (long)M[PF_ALLOC_FRAME];
    out->raw        = (long)M[PF_RAW];
    out->max_depth  = (long)M[PF_MAXDEPTH];
    out->promote    = (long)M[PF_PROMOTE];
    out->lex_direct = (long)M[PF_LEX_DIRECT];
    out->lex_local  = (long)M[PF_LEX_LOCAL];
    out->lex_parent = (long)M[PF_LEX_PARENT];
    out->hash_probes = (long)M[PF_HASH_PROBES];
    out->hash_hits   = (long)M[PF_HASH_HITS];
    out->hash_misses = (long)M[PF_HASH_MISSES];
    out->hash_collisions = (long)M[PF_HASH_COLLISIONS];
    out->hash_fallback = (long)M[PF_HASH_FALLBACK];
    out->hash_fallback_slots = (long)M[PF_HASH_FALLBACK_SLOTS];
}
