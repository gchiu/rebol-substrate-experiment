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
#include "r0_s1_layout.h"   /* GLON-CORE-C2: compile-time layout check */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ============================= loader state ============================ */

static const char *syms[R0S1_SYM_CAP];   /* interner: sym_id -> spelling (profile capacity) */
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
static cell r_set;                          /* GLON-VOCAB-1: masm CALL set */

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
            if (e == HASH_EMPTY || e / R0S1_HASH_RADIX == id) { M[p + ho + idx] = id * R0S1_HASH_RADIX + n; break; }
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

static int  lex_nloc[MAX_LEX_DEPTH];      /* VOCAB-1: locals per scope (below) */
/* VOCAB-1: a scope whose parameters are known by name only: a top-level func of
 * an unwrapped source, which never had a site or T_BOUND references */
static int  lex_pseudo[MAX_LEX_DEPTH];
static void lex_push(void) { if (lex_depth < MAX_LEX_DEPTH) { lex_nloc[lex_depth] = 0; lex_pseudo[lex_depth] = 0; lex_count[lex_depth++] = 0; } }
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
            if (lex_names[si][s] == id) { if (lex_pseudo[si]) return 0; *depth = d; *slot = s; return 1; }
    }
    return 0;
}

/* ==== GLON-VOCAB-1: lexical locals and vocabularies (loader only) ==========
 * Everything here runs in the loader (C); no emitted S1 code changes.
 *
 * Lexical locals.  The set-words of a function body (outside nested literal
 * funcs and dialect blocks) are that function's locals, unless the name is
 * bound by an enclosing function (then it is that binding: closures share it,
 * as before) or -- outside vocabularies only -- a global the reader can see
 * from the source: defined at the top level of the same source, or a DATA
 * global (not a function, native or masm word) that an earlier source has
 * already defined when this one is loaded.  A
 * local is renamed to a DECORATED symbol, its spelling followed by a space,
 * which no source can spell: no global can ever have it, so the runtime's
 * set (nearest existing binding, else a new local) always makes it a local,
 * whatever globals exist now or later.  References to it in the body and in
 * nested functions are renamed alike; parameters keep their names.
 *
 * Vocabularies.  `vocabulary NAME [ body ]` (at a top level: a source, a
 * vocabulary body or a top-level `do [ ]`) declares the vocabulary NAME
 * (nested: PARENT/NAME).  Its members are the body's top-level set-words; each
 * is stored under its full name (s/length, intent/example/face).  Inside the
 * body -- its top level and its functions -- a plain name that is a member
 * means that member (innermost vocabulary first; a member named like a
 * built-in native does not hide the native).  A qualified name is resolved
 * lexically (a nested vocabulary's name, then a top-level vocabulary) and must
 * name an existing member: an unknown member, or a definition (func, masm,
 * does, lambda) made outside the vocabulary, is a load error.  Existing flat
 * globals with the vocabulary's prefix are adopted as members when it is
 * first declared; a source loaded before the declaration is not checked. */
#define LX_DECO ' '
enum { MODE_TOP = 0, MODE_FUNC = 1, MODE_DATA = 2 };
#define MAX_MODE 256
static int mode_stack[MAX_MODE];
static int mode_depth;
/* GLON-C3-2A: blocks nested deeper than MAX_MODE are not recorded (the push
 * is bounded), so they take the deepest recorded mode; reading
 * mode_stack[mode_depth - 1] past the array was an out-of-bounds read
 * (UBSan, 800 nested blocks). */
static int cur_mode(void) {
    if (mode_depth <= 0) return MODE_TOP;
    return mode_stack[(mode_depth < MAX_MODE ? mode_depth : MAX_MODE) - 1];
}
static int next_hint;            /* for the block about to be parsed: 1 code (do/reduce), 2 dialect data */
static int in_spec;              /* parsing a func spec: names are parameters, never rewritten */
static int file_block_pending;   /* the next block is the source's own outer block */
static int func_body_pending;    /* the next block is a (pseudo-scoped) function body */
static int vocab_body_pending;   /* the next block is a vocabulary body */

#define MAX_LOCALS 96
static cell lex_loc[MAX_LEX_DEPTH][MAX_LOCALS];    /* local word ids, per scope */
static cell lex_deco[MAX_LEX_DEPTH][MAX_LOCALS];   /* their decorated ids */

static unsigned char file_top[(R0S1_SYM_CAP + 7) / 8];   /* the source's top-level set-words */
static unsigned char earlier_data[(R0S1_SYM_CAP + 7) / 8]; /* data globals of earlier sources (not functions) */
#define BIT_SET(b, i) ((b)[(i) >> 3] |= (unsigned char)(1u << ((i) & 7)))
#define BIT_GET(b, i) (((b)[(i) >> 3] >> ((i) & 7)) & 1u)
static unsigned char native_bits[(R0S1_SYM_CAP + 7) / 8];
static int native_bits_ready;

/* vocabularies and their members */
#define MAX_VOCABS 128
typedef struct { char path[256]; int parent; } vocab_t;
static vocab_t vocabs[MAX_VOCABS];
static int nvocabs;
#define VM_CAP 16384
/* src: a hash of the source that defined the member (0: adopted from legacy
 * flat definitions); the same source may define it again (a reload) */
typedef struct { int v; cell id; unsigned char defined; unsigned src; unsigned serial; } vmember_t;
static unsigned cur_src;               /* the hash of the source being parsed */
static unsigned cur_serial;            /* which parse this is (a reload is a new one) */
static vmember_t vm_tab[VM_CAP];       /* open addressing, key (v, id) */
static unsigned char vm_used[VM_CAP];
static int vm_log[VM_CAP];             /* table slots in insertion order (rollback) */
static int vm_nlog;
static int vstack[32];
static int vdepth;

static unsigned vm_hash(int v, cell id) { return (unsigned)((unsigned long)id * 2654435761u + (unsigned)v * 40503u) % VM_CAP; }
static vmember_t *vm_find(int v, cell id) {
    unsigned h = vm_hash(v, id);
    for (int k = 0; k < VM_CAP; k++) {
        unsigned i = (h + (unsigned)k) % VM_CAP;
        if (!vm_used[i]) return NULL;
        if (vm_tab[i].v == v && vm_tab[i].id == id) return &vm_tab[i];
    }
    return NULL;
}
static vmember_t *vm_add(int v, cell id) {
    vmember_t *m = vm_find(v, id);
    if (m) return m;
    if (vm_nlog >= VM_CAP - 1) { parse_fail_set(R0S1_PARSE_TOO_LARGE); return NULL; }
    unsigned h = vm_hash(v, id);
    for (int k = 0; k < VM_CAP; k++) {
        unsigned i = (h + (unsigned)k) % VM_CAP;
        if (!vm_used[i]) {
            vm_used[i] = 1; vm_tab[i].v = v; vm_tab[i].id = id; vm_tab[i].defined = 0; vm_tab[i].src = 0; vm_tab[i].serial = 0;
            vm_log[vm_nlog++] = (int)i;
            return &vm_tab[i];
        }
    }
    return NULL;
}
/* a failed parse leaves no vocabulary or member behind */
static void vm_rollback(int nlog, int nv) {
    while (vm_nlog > nlog) {
        vm_nlog--;
        vm_used[vm_log[vm_nlog]] = 0;
    }
    /* rebuild the open-addressing chains of what is left */
    static vmember_t keep[VM_CAP];
    int n = 0;
    for (int i = 0; i < vm_nlog; i++) keep[n++] = vm_tab[vm_log[i]];
    memset(vm_used, 0, sizeof vm_used);
    vm_nlog = 0;
    for (int i = 0; i < n; i++) { vmember_t *m = vm_add(keep[i].v, keep[i].id); if (m) { m->defined = keep[i].defined; m->src = keep[i].src; m->serial = keep[i].serial; } }
    nvocabs = nv;
}
/* "a/b" into d (cap bytes), cut to fit */
static void lx_join(char *d, size_t cap, const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la > cap - 2) la = cap - 2;
    memcpy(d, a, la); d[la] = '/';
    if (lb > cap - la - 2) lb = cap - la - 2;
    memcpy(d + la + 1, b, lb); d[la + 1 + lb] = 0;
}
static int vocab_find(const char *path) {
    for (int i = 0; i < nvocabs; i++) if (strcmp(vocabs[i].path, path) == 0) return i;
    return -1;
}
static cell word_id_of(const char *name) { return word_id(intern(name)); }
static void lx_error(const char *what, const char *name) {
    fprintf(stderr, "r0_s1: %s: %s\n", name, what);
    parse_fail_set(R0S1_PARSE_SYNTAX);
}
/* built-in natives (bound at init) are never hidden by a member */
static int is_native_name(cell id) {
    if (!native_bits_ready) {
        memset(native_bits, 0, sizeof native_bits);
        cell p = r0_untag(global_ctx);
        for (cell k = 0; k < M[p + CTX_COUNT]; k++) {
            cell w = M[p + CTX_DATA + 2 * k], val = M[p + CTX_DATA + 2 * k + 1];
            if (r0_tag(val) == T_NATIVE && word_id(w) < R0S1_SYM_CAP) BIT_SET(native_bits, word_id(w));
        }
        native_bits_ready = 1;
    }
    return id >= 0 && id < R0S1_SYM_CAP && BIT_GET(native_bits, id);
}

/* ---- the pre-scanner: the parser's own tokens, without building anything */
typedef struct { const char *s; int pos; } scan_t;
static int scan_next(scan_t *S, int *st, int *ln) {
    while (S->s[S->pos] && isspace((unsigned char)S->s[S->pos])) S->pos++;
    char c = S->s[S->pos];
    if (!c) return 0;
    if (c == '[' || c == ']') { *st = S->pos++; *ln = 1; return c; }
    if (c == '"') {
        *st = S->pos++;
        while (S->s[S->pos] && S->s[S->pos] != '"') S->pos++;
        if (S->s[S->pos]) S->pos++;
        *ln = S->pos - *st;
        return 's';
    }
    *st = S->pos;
    while (S->s[S->pos] && !isspace((unsigned char)S->s[S->pos]) && S->s[S->pos] != '[' && S->s[S->pos] != ']') S->pos++;
    *ln = S->pos - *st;
    return 'w';
}
static void scan_skip_block(scan_t *S) {          /* just after its '[' */
    int d = 1, st, ln, k;
    while (d > 0 && (k = scan_next(S, &st, &ln)) != 0) { if (k == '[') d++; else if (k == ']') d--; }
}
static int scan_peek(scan_t *S) { int save = S->pos, st, ln; int k = scan_next(S, &st, &ln); S->pos = save; return k; }
static int tok_is(const char *s, int st, int ln, const char *w) { return (int)strlen(w) == ln && strncmp(s + st, w, (size_t)ln) == 0; }
/* a dialect whose block is data (Intent/VID specs: their set-words name faces) */
static int tok_dialect(const char *s, int st, int ln) {
    return tok_is(s, st, ln, "layout") || tok_is(s, st, ln, "panel") || tok_is(s, st, ln, "form") || tok_is(s, st, ln, "tabs");
}
/* `func` just read: its spec block, and its body when that is a literal block */
static void scan_skip_func(scan_t *S) {
    int st, ln;
    if (scan_peek(S) == '[') { scan_next(S, &st, &ln); scan_skip_block(S); }
    if (scan_peek(S) == '[') { scan_next(S, &st, &ln); scan_skip_block(S); }
}
static void scan_skip_masm(scan_t *S) {
    int st, ln, save = S->pos;
    if (scan_next(S, &st, &ln) == 'w' && isdigit((unsigned char)S->s[st])) save = S->pos;
    S->pos = save;
    if (scan_peek(S) == '[') { scan_next(S, &st, &ln); scan_skip_block(S); }
}
static int tok_plain_set(const char *s, int st, int ln) {
    return ln > 1 && s[st + ln - 1] == ':' && s[st] != ':' && s[st] != '\'' && !memchr(s + st, '/', (size_t)(ln - 1));
}
/* the plain set-word names of a function body (S just after its '[') */
static int prescan_func(scan_t *S, cell *out, int cap) {
    int n = 0, d = 1, st, ln, k, prev_dialect = 0;
    while ((k = scan_next(S, &st, &ln)) != 0) {
        if (k == ']') { if (--d == 0) break; prev_dialect = 0; continue; }
        if (k == '[') { if (prev_dialect) scan_skip_block(S); else d++; prev_dialect = 0; continue; }
        if (k == 's') { prev_dialect = 0; continue; }
        prev_dialect = tok_dialect(S->s, st, ln);
        if (tok_is(S->s, st, ln, "func")) { scan_skip_func(S); continue; }
        if (tok_is(S->s, st, ln, "masm") || tok_is(S->s, st, ln, "raw")) { scan_skip_masm(S); continue; }
        if (tok_plain_set(S->s, st, ln)) {
            char buf[64]; int l = ln - 1 < 63 ? ln - 1 : 63;
            memcpy(buf, S->s + st, (size_t)l); buf[l] = 0;
            cell id = word_id_of(buf);
            int seen = 0;
            for (int i = 0; i < n; i++) if (out[i] == id) seen = 1;
            if (!seen && n < cap) out[n++] = id;
        }
    }
    return n;
}
/* the top level of a source or a vocabulary body (S just after its '[', or at
 * the start of an unwrapped source: open = 0): each plain set-word, each
 * set-word of the vocabulary's own full names (prefix "PATH/"), and each
 * nested vocabulary's name, reported through add(); `do [ ]` blocks are the
 * same top level */
typedef void (*top_add_fn)(const char *name, int len, int nested);
/* GLON-VIEW-CORE-V5: a nested vocabulary is pre-declared while its enclosing
 * body is pre-scanned (defined further below) */
static void top_add_member(const char *name, int len, int nested);
static void lx_vocab_predeclare(scan_t *S, const char *name, int len);
static void prescan_top(scan_t *S, int open, const char *prefix, top_add_fn add) {
    int d = open ? 1 : 0, st, ln, k, prev_do = 0;
    size_t pl = prefix ? strlen(prefix) : 0;
    while ((k = scan_next(S, &st, &ln)) != 0) {
        if (k == ']') { if (open && --d == 0) break; prev_do = 0; continue; }
        if (k == '[') { if (prev_do) d++; else scan_skip_block(S); prev_do = 0; continue; }
        if (k == 's') { prev_do = 0; continue; }
        prev_do = tok_is(S->s, st, ln, "do");
        if (tok_is(S->s, st, ln, "func")) { scan_skip_func(S); continue; }
        if (tok_is(S->s, st, ln, "masm") || tok_is(S->s, st, ln, "raw")) { scan_skip_masm(S); continue; }
        if (tok_is(S->s, st, ln, "vocabulary")) {
            int nst, nln, named = 0;
            if (scan_next(S, &nst, &nln) == 'w') { add(S->s + nst, nln, 1); named = 1; }
            if (scan_peek(S) == '[') {
                int bst, bln;
                scan_next(S, &bst, &bln);
                /* GLON-VIEW-CORE-V5: inside a vocabulary body, a nested
                 * vocabulary and its members exist from the start of the body,
                 * like its plain members (a forward qualified reference) */
                if (named && add == top_add_member) lx_vocab_predeclare(S, S->s + nst, nln);
                scan_skip_block(S);
            }
            continue;
        }
        if (tok_plain_set(S->s, st, ln)) { add(S->s + st, ln - 1, 0); continue; }
        if (pl && ln > (int)pl + 1 && S->s[st + ln - 1] == ':' && strncmp(S->s + st, prefix, pl) == 0 &&
            !memchr(S->s + st + pl, '/', (size_t)(ln - 1 - (int)pl)))
            add(S->s + st + pl, ln - 1 - (int)pl, 0);
    }
}
static void top_add_file(const char *name, int len, int nested) {
    (void)nested;
    char buf[64]; int l = len < 63 ? len : 63;
    memcpy(buf, name, (size_t)l); buf[l] = 0;
    cell id = word_id_of(buf);
    if (id >= 0 && id < R0S1_SYM_CAP) BIT_SET(file_top, id);
}
static int top_vocab;                       /* the vocabulary top_add_member adds to */
static int top_dup;                         /* a member defined before this body */
static void top_add_member(const char *name, int len, int nested) {
    char buf[64]; int l = len < 63 ? len : 63;
    memcpy(buf, name, (size_t)l); buf[l] = 0;
    vmember_t *m = vm_find(top_vocab, word_id_of(buf));
    if (m && m->defined == 2 && !nested && (m->src != cur_src || m->serial == cur_serial)) { if (!top_dup) { char full[1024]; lx_join(full, sizeof full, vocabs[top_vocab].path, buf); lx_error("member already defined (a vocabulary defines each member once)", full); } top_dup = 1; return; }
    if (m && m->defined == 2 && !nested) return;      /* this source's own, again (a reload) */
    m = vm_add(top_vocab, word_id_of(buf));
    if (m) { m->defined = 3; m->src = cur_src; m->serial = cur_serial; }   /* this body's: marked 2 when the body is done */
}
/* the decorated (local) id of a name */
static cell lx_deco_id(cell id) {
    const char *nm = syms[id];
    char buf[80];
    snprintf(buf, sizeof buf, "%s%c", nm, LX_DECO);
    return word_id_of(buf);
}
/* a new function scope (the top lex level, its parameters added): its locals */
static void lx_func_locals(const char *src, int body_pos) {
    int d = lex_depth - 1;
    if (d < 0) return;
    cell cand[MAX_LOCALS];
    scan_t S = { src, body_pos + 1 };
    int n = prescan_func(&S, cand, MAX_LOCALS);
    lex_nloc[d] = 0;
    for (int i = 0; i < n; i++) {
        cell id = cand[i];
        int bound = 0;
        for (int s = 0; s < lex_count[d] && !bound; s++) if (lex_names[d][s] == id) bound = 1;   /* a parameter */
        for (int e = 0; e < d && !bound; e++) {
            for (int s = 0; s < lex_count[e]; s++) if (lex_names[e][s] == id) bound = 1;
            for (int s = 0; s < lex_nloc[e]; s++) if (lex_loc[e][s] == id) bound = 1;
        }
        if (bound) continue;
        if (vdepth == 0 && id < R0S1_SYM_CAP && (BIT_GET(file_top, id) || BIT_GET(earlier_data, id)))
            continue;   /* the source's own global, or an earlier source's data global (not a function) */
        if (id < 3) continue;                                                        /* func / return / raw */
        if (lex_nloc[d] >= MAX_LOCALS) break;
        lex_loc[d][lex_nloc[d]] = id;
        lex_deco[d][lex_nloc[d]] = lx_deco_id(id);
        lex_nloc[d]++;
    }
}
/* a plain name in code: 0 not a function binding; 1 a parameter (*depth,*slot);
 * 2 a local (*deco) */
static int lx_func_binding(cell id, int *depth, int *slot, cell *deco) {
    for (int dd = 0; dd < lex_depth; dd++) {
        int si = lex_depth - 1 - dd;
        for (int s = 0; s < lex_count[si]; s++) if (lex_names[si][s] == id) { *depth = dd; *slot = s; return lex_pseudo[si] ? 3 : 1; }
        for (int s = 0; s < lex_nloc[si]; s++) if (lex_loc[si][s] == id) { *deco = lex_deco[si][s]; return 2; }
    }
    return 0;
}
/* the member a plain name means in the enclosing vocabularies, or -1 */
static cell lx_member(cell id) {
    for (int i = vdepth - 1; i >= 0; i--) {
        if (vm_find(vstack[i], id)) {
            if (is_native_name(id)) return -1;
            char full[1024];
            lx_join(full, sizeof full, vocabs[vstack[i]].path, syms[id]);
            return word_id_of(full);
        }
    }
    return -1;
}
/* the next source token after the current position is a definition form */
static int lx_next_is_definition(parser_t *P) {
    scan_t S = { P->s, P->pos };
    int st, ln;
    if (scan_next(&S, &st, &ln) != 'w') return 0;
    return tok_is(P->s, st, ln, "func") || tok_is(P->s, st, ln, "masm") || tok_is(P->s, st, ln, "raw")
        || tok_is(P->s, st, ln, "does") || tok_is(P->s, st, ln, "lambda");
}
/* a qualified name: its full member name, checked; legacy (not a vocabulary)
 * names are left as they are.  kind 's' for a set-word. */
/* 1 if NAME is bound in the global context (no symbol is interned) */
static int lx_global_bound(const char *name) {
    cell p = r0_untag(global_ctx);
    for (cell k = 0; k < M[p + CTX_COUNT]; k++) {
        const char *nm = syms[word_id(M[p + CTX_DATA + 2 * k])];
        if (nm && strcmp(nm, name) == 0) return 1;
    }
    return 0;
}
static cell lx_qualified(parser_t *P, const char *buf, int kind) {
    const char *slash = strchr(buf, '/');
    char head[160];
    size_t hl = (size_t)(slash - buf);
    if (hl == 0 || hl >= sizeof head || !slash[1]) return word_id_of(buf);
    memcpy(head, buf, hl); head[hl] = 0;
    if (strcmp(head, "local") == 0) {            /* a function local by name (reflection, inspection) */
        if (strchr(slash + 1, '/')) { lx_error("malformed qualified name", buf); return word_id_of(buf); }
        return lx_deco_id(word_id_of(slash + 1));
    }
    if (strcmp(head, "global") == 0) {           /* the global binding, past any member */
        if (kind == 's') { lx_error("cannot be assigned: write  set 'name value", buf); return word_id_of(buf); }
        return word_id_of(slash + 1);
    }
    int base = -1;
    cell hid = word_id_of(head);
    for (int i = vdepth - 1; i >= 0 && base < 0; i--) {
        if (!vm_find(vstack[i], hid)) continue;
        char path[1024];
        lx_join(path, sizeof path, vocabs[vstack[i]].path, head);
        base = vocab_find(path);
    }
    if (base < 0) base = vocab_find(head);
    if (base < 0) return word_id_of(buf);        /* not a vocabulary: a flat name, as before */
    const char *rest = slash + 1;
    char seg[160];
    for (;;) {
        const char *nx = strchr(rest, '/');
        size_t sl = nx ? (size_t)(nx - rest) : strlen(rest);
        if (sl == 0 || sl >= sizeof seg) { lx_error("malformed qualified name", buf); return word_id_of(buf); }
        memcpy(seg, rest, sl); seg[sl] = 0;
        cell sid = word_id_of(seg);
        if (nx) {
            char path[1024];
            lx_join(path, sizeof path, vocabs[base].path, seg);
            int nb = vocab_find(path);
            if (nb < 0 || !vm_find(base, sid)) {
                /* GLON-VIEW-CORE-V5 (GLON-VOCAB-1 defect V3): adoption is one
                 * level deep, so a flat global a/b/c adopted with its group a
                 * is no nested vocabulary a/b; the name still names that
                 * global when exactly it is bound */
                char flat[1024];
                lx_join(flat, sizeof flat, vocabs[base].path, rest);
                if (lx_global_bound(flat)) return word_id_of(flat);
                lx_error("no such vocabulary", buf); return word_id_of(buf);
            }
            base = nb; rest = nx + 1; continue;
        }
        vmember_t *m = vm_find(base, sid);
        int own = vdepth > 0 && vstack[vdepth - 1] == base && cur_mode() == MODE_TOP && kind == 's';
        if (!m) {
            if (own) { m = vm_add(base, sid); if (m) { m->defined = 3; m->src = cur_src; m->serial = cur_serial; } }
            else {
                char why[1600];
                snprintf(why, sizeof why, "no member '%s' in vocabulary %s (members are declared in its vocabulary body)", seg, vocabs[base].path);
                lx_error(why, buf);
                return word_id_of(buf);
            }
        } else if (kind == 's' && !own && (vdepth == 0 || vstack[vdepth - 1] != base) && m->src != 0 && !(m->src == cur_src && m->serial != cur_serial) &&
                   lx_next_is_definition(P)) {
            char why[1600];
            snprintf(why, sizeof why, "defined outside vocabulary %s (its members are defined in its body)", vocabs[base].path);
            lx_error(why, buf);
        }
        char full[1024];
        lx_join(full, sizeof full, vocabs[base].path, seg);
        return word_id_of(full);
    }
}
/* declare (or reopen) the vocabulary NAME inside the current scope; its
 * members from the body; answers its index or -1 */
/* create the vocabulary PATH (enclosing vocabulary PARENT, or -1), adopting
 * the flat globals already defined under its prefix; answers its index or -1 */
static int lx_vocab_create(const char *path, int parent) {
    if (nvocabs >= MAX_VOCABS) { lx_error("too many vocabularies", path); return -1; }
    if (strlen(path) >= sizeof vocabs[0].path) { lx_error("vocabulary path too long", path); return -1; }
    int v = nvocabs++;
    memcpy(vocabs[v].path, path, strlen(path) + 1);
    vocabs[v].parent = parent;
    /* adopt the flat globals already defined under its prefix */
    size_t pl = strlen(path);
    cell p = r0_untag(global_ctx);
    for (cell k = 0; k < M[p + CTX_COUNT]; k++) {
        const char *nm = syms[word_id(M[p + CTX_DATA + 2 * k])];
        if (strncmp(nm, path, pl) == 0 && nm[pl] == '/' && nm[pl + 1] && !strchr(nm + pl + 1, '/')) {
            vmember_t *m = vm_add(v, word_id_of(nm + pl + 1));
            if (m) m->defined = 2;
        }
    }
    return v;
}
static int lx_vocab_open(const char *name, const char *src, int body_pos) {
    char path[1024];
    if (vdepth > 0) lx_join(path, sizeof path, vocabs[vstack[vdepth - 1]].path, name);
    else snprintf(path, sizeof path, "%s", name);
    if (strchr(name, '/') || strchr(name, ':') || name[0] == '\'' || name[0] == ':') { lx_error("a vocabulary name is a plain word", name); return -1; }
    int v = vocab_find(path);
    if (v < 0) {
        v = lx_vocab_create(path, vdepth > 0 ? vstack[vdepth - 1] : -1);
        if (v < 0) return -1;
    }
    if (vdepth > 0) { vmember_t *m = vm_add(vstack[vdepth - 1], word_id_of(name)); if (m && m->defined < 2) m->defined = 3; }
    top_vocab = v; top_dup = 0;
    scan_t S = { src, body_pos + 1 };
    char prefix[1100];
    snprintf(prefix, sizeof prefix, "%s/", path);
    prescan_top(&S, 1, prefix, top_add_member);
    return v;
}
/* GLON-VIEW-CORE-V5 (GLON-VOCAB-1 defects V1/V2): called while the body of
 * vocabulary top_vocab is pre-scanned, at a nested `vocabulary NAME [` (S just
 * after its '['): declare the nested vocabulary now and pre-scan its members,
 * so a qualified reference earlier in the enclosing body (relative
 * `point/sum` or full `geo/point/sum`) resolves to the member, as a plain
 * member already does.  The real declaration later finds it registered.  A
 * name that is not a plain word is left to that declaration to refuse. */
static void lx_vocab_predeclare(scan_t *S, const char *name, int len) {
    char nm[64], path[1024], prefix[1100];
    if (len <= 0 || len > 63) return;
    memcpy(nm, name, (size_t)len); nm[len] = 0;
    if (strchr(nm, '/') || strchr(nm, ':') || nm[0] == '\'' || nm[0] == ':') return;
    lx_join(path, sizeof path, vocabs[top_vocab].path, nm);
    int v = vocab_find(path);
    if (v < 0) v = lx_vocab_create(path, top_vocab);
    if (v < 0) return;
    int save_vocab = top_vocab, save_dup = top_dup;
    top_vocab = v; top_dup = 0;
    scan_t S2 = { S->s, S->pos };
    snprintf(prefix, sizeof prefix, "%s/", path);
    prescan_top(&S2, 1, prefix, top_add_member);
    top_vocab = save_vocab; top_dup = save_dup;
}
/* the body is done: its members are defined */
static void lx_vocab_close(int v) {
    for (int i = 0; i < vm_nlog; i++) { vmember_t *m = &vm_tab[vm_log[i]]; if (m->v == v && m->defined == 3) m->defined = 2; }
}

static void skip_ws(parser_t *P) {
    while (P->s[P->pos] && isspace((unsigned char)P->s[P->pos])) P->pos++;
}

static cell parse_form(parser_t *P);

static cell parse_int(parser_t *P) {
    int neg = 0;
    if (P->s[P->pos] == '-') { neg = 1; P->pos++; }
    /* cell, not long: Windows is LLP64, where long is 32-bit and a legal
     * Glon literal above 2^27 would be truncated before tagging. */
    cell n = 0;
    while (P->s[P->pos] >= '0' && P->s[P->pos] <= '9') { n = n * 10 + (P->s[P->pos] - '0'); P->pos++; }
    return mk_int(neg ? -n : n);
}

static cell parse_word(parser_t *P) {
    int start = P->pos;
    while (P->s[P->pos] && !isspace((unsigned char)P->s[P->pos])
           && P->s[P->pos] != '[' && P->s[P->pos] != ']') P->pos++;
    int len = P->pos - start;
    /* An empty token means a form was required at a `]` or at the end of the
     * source: a stray top-level `]` (parse_program), or `func` / `masm` with
     * nothing after them in a block. No Glon value has an empty spelling, so
     * this is malformed source (SYNTAX); never index buf[len - 1] with len 0. */
    if (len == 0) { P->err = 1; return R0_NONE; }
    char buf[64];
    if (len > 63) len = 63;
    memcpy(buf, P->s + start, (size_t)len);
    buf[len] = 0;
    if (len == 4 && strcmp(buf, "none") == 0) return R0_NONE;
    /* GLON-VOCAB-1: names in code are resolved lexically (locals, members,
     * qualified names); a lit-word is data and keeps its spelling */
    int rewrite = !in_spec && cur_mode() != MODE_DATA;
    if (buf[len - 1] == ':') {
        buf[len - 1] = 0;
        if (strchr(buf, '/') && len > 2) return mk_set(lx_qualified(P, buf, 's'));
        cell id = word_id(intern(buf));
        if (rewrite && id >= 3) {
            int depth, slot; cell deco;
            int b = lx_func_binding(id, &depth, &slot, &deco);
            if (b == 2) return mk_set(deco);
            if (b == 0 && lex_depth == 0 && vdepth > 0 && cur_mode() == MODE_TOP) {
                /* a member definition at the vocabulary's top level */
                char full[1024];
                lx_join(full, sizeof full, vocabs[vstack[vdepth - 1]].path, buf);
                return mk_set(word_id_of(full));
            }
        }
        return mk_set(id);
    }
    if (buf[0] == ':' && len > 1) {
        if (strchr(buf + 1, '/')) return mk_get(lx_qualified(P, buf + 1, 'g'));
        cell id = word_id(intern(buf + 1));
        if (rewrite && id >= 3) {
            int depth, slot; cell deco;
            int b = lx_func_binding(id, &depth, &slot, &deco);
            if (b == 2) return mk_get(deco);
            if (b == 0) { cell m = lx_member(id); if (m >= 0) return mk_get(m); }
        }
        return mk_get(id);
    }
    if (buf[0] == '\'') {
        /* GLON-VOCAB-1: 'local/NAME is the local NAME's own (decorated) word */
        if (strncmp(buf + 1, "local/", 6) == 0 && buf[7] && !strchr(buf + 7, '/')) return mk_lit(lx_deco_id(word_id_of(buf + 7)));
        return mk_lit((cell)word_id(intern(buf + 1)));
    }
    if (len > 1 && strchr(buf, '/') && buf[0] != '/' && buf[len - 1] != '/') {
        if (!in_spec) return mk_word(lx_qualified(P, buf, 'w'));
    }
    {
        cell id = word_id(intern(buf));
        if (rewrite && id >= 3) {
            int depth, slot; cell deco;
            int b = lx_func_binding(id, &depth, &slot, &deco);
            if (b == 1) return mk_bound(depth, slot);
            if (b == 2) return mk_word(deco);
            if (b == 3) return mk_word(id);
            cell m = lx_member(id);
            if (m >= 0) return mk_word(m);
            return mk_word(id);
        }
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
static void ds_floor_update(void); /* forward decl (defined with the stack guards) */

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
    /* GLON-VOCAB-1: the explicit global assignment word (set) */
    { "RV_WORD", RV_WORD }, { "GC_GLOBAL_CTX", GC_GLOBAL_CTX },
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
    /* GLON-C3-2: the loadable task vocabulary's fields and states (task.glon).
     * Numeric layout constants only; scheduling policy stays in Glon. */
    { "TREC_BLOCKED", TREC_BLOCKED }, { "TREC_GEN", TREC_GEN },
    { "TREC_MINSP", TREC_MINSP }, { "TREC_MINRP", TREC_MINRP }, { "TREC_RESP", TREC_RESP },
    { "M1_MAX_TASKS", M1_MAX_TASKS }, { "M1_STRESS_CNT", M1_STRESS_CNT },
    { "TASK_RUNNABLE", TASK_RUNNABLE }, { "TASK_FINISHED", TASK_FINISHED },
    { "TASK_EMPTY", TASK_EMPTY },
};
#define N_RAW_SYMS ((int)(sizeof raw_syms / sizeof raw_syms[0]))

/* MASM-PRIVATE-NAMES: masm is a private sublanguage. A masm block is read by
 * the assembler's own tokenizer (masm_read_block), not by the Glon reader, so
 * its mnemonics, operands, symbolic registers and labels are never interned
 * in the Glon symbol table and never lexically resolved against an enclosing
 * func's parameters: a masm spelling may equal any Glon word or parameter
 * without interacting with it. A token keeps its spelling as a slice of the
 * source being parsed, which outlives the assembly. */
enum { MT_OTHER, MT_INT, MT_WORD, MT_LABEL };
typedef struct { int kind; cell ival; const char *s; int len; } masm_tok_t;
#define MASM_TOK_CAP 511                      /* the Glon reader's block limit */
static masm_tok_t masm_toks[MASM_TOK_CAP];

static int mt_is(const masm_tok_t *t, const char *name) {
    return t->kind == MT_WORD && (size_t)t->len == strlen(name)
        && strncmp(t->s, name, (size_t)t->len) == 0;
}

/* a token's spelling as a C string, for diagnostics (the no-libc WASM build's
 * printf has no %.*s) */
static const char *mt_name(const char *s, int len) {
    static char buf[64];
    memcpy(buf, s, (size_t)len);
    buf[len] = 0;
    return buf;
}

/* resolve an operand token to a cell/offset: an integer literal, or a
 * symbolic ABI name. Returns 0 and sets *ok=0 if unresolved. */
static cell raw_resolve(const masm_tok_t *tok, int *ok) {
    if (tok->kind == MT_INT) { *ok = 1; return tok->ival; }
    if (tok->kind == MT_WORD) {
        for (int i = 0; i < N_RAW_SYMS; i++)
            if (mt_is(tok, raw_syms[i].name)) { *ok = 1; return raw_syms[i].value; }
    }
    *ok = 0;
    return 0;
}

/* Per-block label and forward-reference tables. A block may hold up to
 * MASM_TOK_CAP tokens, more than either table, so the parser counts a block's
 * labels and references (raw_counts) and refuses it as too_large before
 * anything is emitted; assemble_raw also checks each index (an internal error
 * if the parser's count was wrong). */
#define RAW_LABEL_CAP 64
#define RAW_REF_CAP   128
typedef struct { const char *name; int len; cell addr; } raw_label_t;
static raw_label_t raw_labels[RAW_LABEL_CAP];
static int raw_nlabels;
typedef struct { cell patch; const char *name; int len; } raw_ref_t;
static raw_ref_t raw_refs[RAW_REF_CAP];
static int raw_nrefs;

static cell raw_find_label(const char *name, int len) {
    for (int i = 0; i < raw_nlabels; i++)
        if (raw_labels[i].len == len && strncmp(raw_labels[i].name, name, (size_t)len) == 0)
            return raw_labels[i].addr;
    return -1;
}

static void raw_add_ref(cell patch, const masm_tok_t *op) {
    if (raw_nrefs >= RAW_REF_CAP) {
        fprintf(stderr, "r0_s1: internal error: masm reference table overflow\n");
        abort();
    }
    raw_refs[raw_nrefs].patch = patch;
    raw_refs[raw_nrefs].name = op->s;
    raw_refs[raw_nrefs].len = op->len;
    raw_nrefs++;
}

/* Read the masm block `[ ... ]` at P->pos into masm_toks with the Glon
 * reader's token rules (integers; a word ends at whitespace or a bracket and
 * is cut to 63 bytes; `name:` is a label), but without interning or lexical
 * resolution. Forms that are neither instructions nor operands (`none`,
 * `:get` and `'lit` forms, nested blocks, string literals) become inert
 * tokens, which the assembler skips as before. Returns the token count, or
 * -1 with P->err set. */
static int masm_read_block(parser_t *P) {
    int n = 0;
    P->pos++;                                     /* '[' */
    for (;;) {
        skip_ws(P);
        char c = P->s[P->pos];
        if (c == ']') { P->pos++; return n; }
        if (c == '\0') { P->err = 1; return -1; }
        if (n >= MASM_TOK_CAP) { parse_fail_set(R0S1_PARSE_TOO_LARGE); P->err = 1; return -1; }
        masm_tok_t *t = &masm_toks[n++];
        t->kind = MT_OTHER; t->ival = int_val(R0_NONE); t->s = ""; t->len = 0;
        if (c == '[') {                           /* nested block: inert */
            int depth = 0;
            do {
                if (P->s[P->pos] == '\0') { P->err = 1; return -1; }
                if (P->s[P->pos] == '[') depth++;
                else if (P->s[P->pos] == ']') depth--;
                P->pos++;
            } while (depth > 0);
            continue;
        }
        if (c == '"') {                           /* string literal: inert */
            P->pos++;
            while (P->s[P->pos] && P->s[P->pos] != '"') P->pos++;
            if (P->s[P->pos] != '"') { P->err = 1; return -1; }
            P->pos++;
            continue;
        }
        if ((c >= '0' && c <= '9') || (c == '-' && isdigit((unsigned char)P->s[P->pos + 1]))) {
            t->kind = MT_INT;
            t->ival = int_val(parse_int(P));
            continue;
        }
        int start = P->pos;
        while (P->s[P->pos] && !isspace((unsigned char)P->s[P->pos])
               && P->s[P->pos] != '[' && P->s[P->pos] != ']') P->pos++;
        int len = P->pos - start;
        if (len > 63) len = 63;
        const char *s = P->s + start;
        if (len == 4 && strncmp(s, "none", 4) == 0) continue;
        if (s[len - 1] == ':') { t->kind = MT_LABEL; t->s = s; t->len = len - 1; continue; }
        if (s[0] == ':' || s[0] == '\'') continue;
        t->kind = MT_WORD; t->s = s; t->len = len;
    }
}

/* The exact number of cells assemble_raw will emit for tk[0..n), computed
 * before anything is emitted. It follows assemble_raw's dispatch token for
 * token; the per-form sizes are those of the frozen s1.c assembler (LIT, HOST
 * and ZBRANCH 2 cells; DUP DROP @ ! 1; asm_branch / asm_branch_fwd 5; asm_toR /
 * asm_fromR 14; asm_exit 17; asm_call / emit_call_fwd 21), which cannot change.
 * The parser checks the result against R0S1_CODE_LIMIT before assembling, and
 * after assembling that the emitted size is exactly this. */
static cell raw_size(const masm_tok_t *tk, int n) {
    const masm_tok_t none_tok = { MT_OTHER, int_val(R0_NONE), "", 0 };
    cell size = 0;
    for (int i = 0; i < n; i++) {
        const masm_tok_t *tok = &tk[i];
        if (tok->kind != MT_WORD) continue;                 /* labels emit nothing */
        if (mt_is(tok, "LIT") || mt_is(tok, "INT") || mt_is(tok, "ARITY")
            || mt_is(tok, "HOST") || mt_is(tok, "ZBRANCH")
            || mt_is(tok, "BRANCH") || mt_is(tok, "CALL")) {
            i++;
            const masm_tok_t *op = (i < n) ? &tk[i] : &none_tok;
            if (mt_is(tok, "LIT") || mt_is(tok, "INT")
                || mt_is(tok, "ARITY") || mt_is(tok, "HOST")) {
                int ok; (void)raw_resolve(op, &ok);
                if (ok) size += 2;                          /* unresolved: nothing emitted */
            } else if (mt_is(tok, "ZBRANCH")) size += 2;
            else if (mt_is(tok, "BRANCH")) size += 5;
            else size += 21;                                /* CALL, every form */
            continue;
        }
        if (mt_is(tok, "DUP") || mt_is(tok, "DROP") || mt_is(tok, "@") || mt_is(tok, "!")) size += 1;
        else if (mt_is(tok, "EXIT")) size += 17;
        else if (mt_is(tok, ">R") || mt_is(tok, "R>")) size += 14;
        else if (mt_is(tok, "NONE") || mt_is(tok, "ADD") || mt_is(tok, "SUB") || mt_is(tok, "MUL")
                 || mt_is(tok, "DIV") || mt_is(tok, "MOD") || mt_is(tok, "EQ") || mt_is(tok, "NE")
                 || mt_is(tok, "LT") || mt_is(tok, "GT") || mt_is(tok, "LE") || mt_is(tok, "GE")
                 || mt_is(tok, "PRINT") || mt_is(tok, "ALLOC")) size += 2;
        /* an unknown mnemonic emits nothing (assemble_raw reports it) */
    }
    return size;
}

/* How many label definitions and label references assemble_raw will record
 * for tk[0..n): it follows assemble_raw's dispatch (a ZBRANCH, BRANCH or CALL
 * whose operand is a word other than CALL's named runtime entry points adds a
 * reference). */
static void raw_counts(const masm_tok_t *tk, int n, int *labels, int *refs) {
    *labels = 0; *refs = 0;
    for (int i = 0; i < n; i++) {
        const masm_tok_t *tok = &tk[i];
        if (tok->kind == MT_LABEL) { (*labels)++; continue; }
        if (tok->kind != MT_WORD) continue;
        if (mt_is(tok, "LIT") || mt_is(tok, "INT") || mt_is(tok, "ARITY")
            || mt_is(tok, "HOST") || mt_is(tok, "ZBRANCH")
            || mt_is(tok, "BRANCH") || mt_is(tok, "CALL")) {
            int is_ref = mt_is(tok, "ZBRANCH") || mt_is(tok, "BRANCH") || mt_is(tok, "CALL");
            int is_call = mt_is(tok, "CALL");
            i++;
            if (i >= n || !is_ref || tk[i].kind != MT_WORD) continue;
            const masm_tok_t *op = &tk[i];
            if (is_call && (mt_is(op, "alloc") || mt_is(op, "collect") || mt_is(op, "lookup")
                            || mt_is(op, "set")
                            || mt_is(op, "ESC_ANY") || mt_is(op, "ESC_TRANSPORT"))) continue;
            (*refs)++;
        }
    }
}

/* Validate a masm block before anything is emitted (K6, K7; 2026-11-10).
 * assemble_raw used to skip what it could not assemble, with only a warning:
 * an unknown mnemonic or a stray token, an instruction without its operand,
 * an unresolved operand, a HOST id outside S1's closed set, an undefined label
 * (patched as address 0) or a label defined twice. Each is now an error, and
 * so is a block that can run past its own end: an empty block (its entry
 * would be the end of code), or one whose last instruction is not EXIT,
 * BRANCH or an indirect jump (LIT REG_IP !), or that defines a label after
 * its last instruction (falling off the end runs whatever follows). Answers
 * 0, or -1 after printing the reason. */
static int raw_is_mnemonic(const masm_tok_t *t) {
    static const char *const ops[] = {
        "LIT", "INT", "ARITY", "HOST", "ZBRANCH", "BRANCH", "CALL",
        "DUP", "DROP", "@", "!", "EXIT", ">R", "R>", "NONE",
        "ADD", "SUB", "MUL", "DIV", "MOD", "EQ", "NE", "LT", "GT", "LE", "GE",
        "PRINT", "ALLOC" };
    for (unsigned i = 0; i < sizeof ops / sizeof ops[0]; i++) if (mt_is(t, ops[i])) return 1;
    return 0;
}
static int raw_is_named_entry(const masm_tok_t *t) {
    return mt_is(t, "alloc") || mt_is(t, "collect") || mt_is(t, "lookup") || mt_is(t, "set")
        || mt_is(t, "ESC_ANY") || mt_is(t, "ESC_TRANSPORT");
}
static int raw_validate(const masm_tok_t *tk, int n) {
    int last = -1;                      /* index of the last instruction token */
    int label_after_last = 0;
    for (int i = 0; i < n; i++) {
        const masm_tok_t *t = &tk[i];
        if (t->kind == MT_LABEL) {
            for (int j = 0; j < i; j++)
                if (tk[j].kind == MT_LABEL && tk[j].len == t->len && strncmp(tk[j].s, t->s, (size_t)t->len) == 0) {
                    fprintf(stderr, "r0_s1: invalid masm block: label '%s' is defined twice\n", mt_name(t->s, t->len));
                    return -1;
                }
            label_after_last = 1;
            continue;
        }
        if (t->kind != MT_WORD) {
            fprintf(stderr, "r0_s1: invalid masm block: token %d is not an instruction (a number, block, string or other form)\n", i + 1);
            return -1;
        }
        if (!raw_is_mnemonic(t)) {
            fprintf(stderr, "r0_s1: invalid masm block: unknown mnemonic '%s'\n", mt_name(t->s, t->len));
            return -1;
        }
        last = i; label_after_last = 0;
        int lit = mt_is(t, "LIT") || mt_is(t, "INT") || mt_is(t, "ARITY") || mt_is(t, "HOST");
        int jump = mt_is(t, "ZBRANCH") || mt_is(t, "BRANCH") || mt_is(t, "CALL");
        if (!lit && !jump) continue;
        if (i + 1 >= n) { fprintf(stderr, "r0_s1: invalid masm block: '%s' has no operand\n", mt_name(t->s, t->len)); return -1; }
        const masm_tok_t *op = &tk[++i];
        if (lit) {
            int ok; cell v = raw_resolve(op, &ok);
            if (!ok) {
                fprintf(stderr, "r0_s1: invalid masm block: '%s' has an unresolved operand (token %d)\n", mt_name(t->s, t->len), i + 1);
                return -1;
            }
            if (mt_is(t, "HOST") && (v < 0 || v > HOST_DUMP)) {
                fprintf(stderr, "r0_s1: invalid masm block: HOST %ld is not an S1 host operation (0..%d)\n", (long)v, (int)HOST_DUMP);
                return -1;
            }
            if (mt_is(t, "ARITY") && v < 0) { fprintf(stderr, "r0_s1: invalid masm block: ARITY %ld is negative\n", (long)v); return -1; }
            continue;
        }
        if (op->kind == MT_INT) continue;                        /* an absolute address (RAW) */
        if (op->kind != MT_WORD) {
            fprintf(stderr, "r0_s1: invalid masm block: '%s' has no target (token %d)\n", mt_name(t->s, t->len), i + 1);
            return -1;
        }
        if (mt_is(t, "CALL") && raw_is_named_entry(op)) continue;
        int found = 0;
        for (int j = 0; j < n && !found; j++)
            if (tk[j].kind == MT_LABEL && tk[j].len == op->len && strncmp(tk[j].s, op->s, (size_t)op->len) == 0) found = 1;
        if (!found) {
            fprintf(stderr, "r0_s1: invalid masm block: label '%s' is not defined in this block\n", mt_name(op->s, op->len));
            return -1;
        }
    }
    if (last < 0) { fprintf(stderr, "r0_s1: invalid masm block: the block is empty (it would enter the end of code)\n"); return -1; }
    /* the third unconditional transfer: an indirect jump, `LIT REG_IP !`
     * (the M1 scheduler, stack-switching and tail-jump idiom) */
    int ind_jump = mt_is(&tk[last], "!") && last >= 2 && mt_is(&tk[last - 2], "LIT")
                && mt_is(&tk[last - 1], "REG_IP");
    if (!mt_is(&tk[last], "EXIT") && !mt_is(&tk[last], "BRANCH") && !ind_jump) {
        fprintf(stderr, "r0_s1: invalid masm block: the last instruction is not EXIT, BRANCH or an indirect jump (LIT REG_IP !): "
                           "execution would run past the block\n");
        return -1;
    }
    if (label_after_last) {
        fprintf(stderr, "r0_s1: invalid masm block: a label is defined after the last instruction (it would enter the end of the block)\n");
        return -1;
    }
    return 0;
}

static cell assemble_raw(const masm_tok_t *tk, int n) {
    const masm_tok_t none_tok = { MT_OTHER, int_val(R0_NONE), "", 0 };
    raw_nlabels = 0; raw_nrefs = 0;
    cell entry = asm_here();
    for (int i = 0; i < n; i++) {
        const masm_tok_t *tok = &tk[i];
        if (tok->kind == MT_LABEL) {      /* label definition */
            if (raw_nlabels >= RAW_LABEL_CAP) {
                fprintf(stderr, "r0_s1: internal error: masm label table overflow\n");
                abort();
            }
            raw_labels[raw_nlabels].name = tok->s;
            raw_labels[raw_nlabels].len = tok->len;
            raw_labels[raw_nlabels].addr = asm_here();
            raw_nlabels++;
            continue;
        }
        if (tok->kind != MT_WORD) continue;
        if (mt_is(tok, "LIT") || mt_is(tok, "INT") || mt_is(tok, "ARITY")
            || mt_is(tok, "HOST") || mt_is(tok, "ZBRANCH")
            || mt_is(tok, "BRANCH") || mt_is(tok, "CALL")) {
            i++;
            const masm_tok_t *op = (i < n) ? &tk[i] : &none_tok;
            if (mt_is(tok, "LIT") || mt_is(tok, "INT")
                || mt_is(tok, "ARITY") || mt_is(tok, "HOST")) {
                int ok; cell v = raw_resolve(op, &ok);
                if (!ok) {
                    fprintf(stderr, "r0_s1: unresolved RAW operand in '%s'\n", mt_name(tok->s, tok->len));
                    continue;
                }
                if (mt_is(tok, "LIT")) asm_lit(v);
                else if (mt_is(tok, "INT")) asm_lit(mk_int(v));
                else if (mt_is(tok, "ARITY")) asm_lit(mk_int(v));
                else asm_host((int)v);
            } else if (mt_is(tok, "ZBRANCH")) {
                if (op->kind == MT_WORD) raw_add_ref(asm_zbranch_fwd(), op);
                else asm_zbranch((int)op->ival);
            } else if (mt_is(tok, "BRANCH")) {
                if (op->kind == MT_WORD) raw_add_ref(asm_branch_fwd(), op);
                else asm_branch((int)op->ival);
            } else if (mt_is(tok, "CALL")) {
                if (op->kind == MT_WORD) {
                    /* symbolic managed-heap entry points (G1 STRING! primitives) */
                    if (mt_is(op, "alloc")) asm_call(r_alloc);
                    else if (mt_is(op, "collect")) asm_call(r_collect);
                    else if (mt_is(op, "lookup")) asm_call(r_lookup);
                    else if (mt_is(op, "set")) asm_call(r_set);         /* GLON-VOCAB-1 */
                    else if (mt_is(op, "ESC_ANY")) asm_call(r_esc_any);
                    else if (mt_is(op, "ESC_TRANSPORT")) asm_call(r_esc_transport);
                    else raw_add_ref(emit_call_fwd(), op);
                }
                else asm_call((int)op->ival);
            }
            continue;
        }
        if (mt_is(tok, "DUP")) asm_dup();
        else if (mt_is(tok, "DROP")) asm_drop();
        else if (mt_is(tok, "@")) asm_fetch();
        else if (mt_is(tok, "!")) asm_store();
        else if (mt_is(tok, "EXIT")) asm_exit();
        else if (mt_is(tok, ">R")) asm_toR();
        else if (mt_is(tok, "R>")) asm_fromR();
        else if (mt_is(tok, "NONE")) asm_lit(R0_NONE);
        else if (mt_is(tok, "ADD")) asm_host(HOST_ADD);
        else if (mt_is(tok, "SUB")) asm_host(HOST_SUB);
        else if (mt_is(tok, "MUL")) asm_host(HOST_MUL);
        else if (mt_is(tok, "DIV")) asm_host(HOST_DIV);
        else if (mt_is(tok, "MOD")) asm_host(HOST_MOD);
        else if (mt_is(tok, "EQ")) asm_host(HOST_EQ);
        else if (mt_is(tok, "NE")) asm_host(HOST_NE);
        else if (mt_is(tok, "LT")) asm_host(HOST_LT);
        else if (mt_is(tok, "GT")) asm_host(HOST_GT);
        else if (mt_is(tok, "LE")) asm_host(HOST_LE);
        else if (mt_is(tok, "GE")) asm_host(HOST_GE);
        else if (mt_is(tok, "PRINT")) asm_host(HOST_PRINT);
        else if (mt_is(tok, "ALLOC")) asm_host(HOST_ALLOC);
        else fprintf(stderr, "r0_s1: unknown RAW mnemonic '%s'\n", mt_name(tok->s, tok->len));
    }
    for (int i = 0; i < raw_nrefs; i++) {
        cell a = raw_find_label(raw_refs[i].name, raw_refs[i].len);
        if (a < 0) { fprintf(stderr, "r0_s1: undefined RAW label '%s'\n", mt_name(raw_refs[i].name, raw_refs[i].len)); continue; }
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

/* Deferred masm emission (2026-11-10). A source's masm blocks are emitted
 * only when the whole source has parsed: until then each block is recorded
 * by the position of its `[` in the source, its exact size (raw_size) and the
 * entry address it will have (the code end plus the sizes of the blocks
 * before it). The callable [entry, arity] gets that predicted entry. At the
 * end of a successful parse the blocks are read again from the same source
 * (the masm tokenizer is deterministic) and assembled in order; each must land
 * exactly at its predicted entry with its computed size. A parse that fails
 * for any reason, at any point, therefore emits no code: the frozen
 * assembler's cursor never has to move back. Blocks of size 0 emit nothing
 * and are not recorded. At most RAW_PENDING_CAP blocks per source (each
 * emits at least one cell; the code space holds about 3,200). */
#define RAW_PENDING_CAP 1024
typedef struct { int pos; cell need, at; } raw_pending_t;
static raw_pending_t raw_pending[RAW_PENDING_CAP];
static int raw_npending;
static cell raw_pending_cells;

/* `masm` (canonical) or `raw` (alias): P->pos is just after the keyword.
 * Reads the optional arity and the assembly block, checks the block's tables
 * and size, makes the 2-cell callable [entry, arity] and records the block
 * for emission when the whole source has parsed (raw_pending). Answers the
 * callable in *out, or -1 with P->err (and parse_fail) set. Shared by blocks
 * (parse_block) and unwrapped top-level sources (parse_program), so that a
 * masm definition means the same in both. */
static int parse_masm(parser_t *P, cell *out) {
    int arity = 0;
    /* optional integer arity before the assembly block */
    int save = P->pos;
    skip_ws(P);
    if (P->s[P->pos] >= '0' && P->s[P->pos] <= '9')
        arity = (int)int_val(parse_int(P));
    else
        P->pos = save;
    /* The assembly block is read by masm's own tokenizer, never as a Glon
     * block (MASM-PRIVATE-NAMES): it is not interned, not lexically
     * resolved, and takes no loader-heap space. Only the 2-cell callable
     * [entry, arity] survives. The operand must be a block. */
    skip_ws(P);
    if (P->s[P->pos] != '[') { P->err = 1; return -1; }   /* no block: never assemble */
    int block_pos = P->pos;
    int ntok = masm_read_block(P);            /* [ instr... ] */
    if (P->err) return -1;
    /* Well-formedness (raw_validate): refused as a syntax error, before
     * anything is emitted or recorded. */
    if (raw_validate(masm_toks, ntok) < 0) {
        parse_fail_set(R0S1_PARSE_SYNTAX);
        P->err = 1;
        return -1;
    }
    /* Table bounds: a block with more labels or label references than the
     * assembler's tables hold is refused as too_large. */
    {
        int nlab, nref;
        raw_counts(masm_toks, ntok, &nlab, &nref);
        if (nlab > RAW_LABEL_CAP || nref > RAW_REF_CAP) {
            fprintf(stderr, "r0_s1: masm block too large: %d labels (at most %d), "
                            "%d label references (at most %d)\n",
                    nlab, RAW_LABEL_CAP, nref, RAW_REF_CAP);
            parse_fail_set(R0S1_PARSE_TOO_LARGE);
            P->err = 1;
            return -1;
        }
    }
    /* Code-space bound: refuse a block whose code would end above
     * R0S1_CODE_LIMIT (the data stack keeps R0S1_DS_RESERVE cells). The parse
     * then fails as too_large and is rolled back like any failed parse; no
     * code is emitted and no binding is made, because the source never runs. */
    cell need = raw_size(masm_toks, ntok);
    cell at = asm_here() + raw_pending_cells;   /* after this source's earlier blocks */
    if (at + need > R0S1_CODE_LIMIT) {
        fprintf(stderr, "r0_s1: masm code space exhausted: the block needs %ld cells, "
                        "%ld are free below the code limit %ld\n",
                (long)need, (long)(R0S1_CODE_LIMIT - at), (long)R0S1_CODE_LIMIT);
        parse_fail_set(R0S1_PARSE_TOO_LARGE);
        P->err = 1;
        return -1;
    }
    if (need > 0 && raw_npending >= RAW_PENDING_CAP) {
        fprintf(stderr, "r0_s1: masm: more than %d blocks in one source\n", RAW_PENDING_CAP);
        parse_fail_set(R0S1_PARSE_TOO_LARGE);
        P->err = 1;
        return -1;
    }
    cell p = lalloc(2);
    if (p < 0) {
        parse_fail_set(R0S1_PARSE_LOADER_EXHAUSTED);
        P->err = 1;
        return -1;
    }
    if (need > 0) {                           /* emitted when the source has parsed */
        raw_pending[raw_npending].pos = block_pos;
        raw_pending[raw_npending].need = need;
        raw_pending[raw_npending].at = at;
        raw_npending++;
        raw_pending_cells += need;
    }
    M[p + RAW_ENTRY] = at;
    M[p + RAW_ARITY] = (cell)arity;
    *out = mk_raw(p);
    return 0;
}

/* A block (and a top-level program) holds at most 511 cells.  Each append
 * of k cells is checked BEFORE it is made: a string literal adds 2, a func
 * up to 3, so a check after the append (as it was) let them write past
 * tmp[511] -- a stack overrun instead of TOO_LARGE.  A failed parse is
 * rolled back completely (r0_s1_parse), so failing here is safe. */
#define PB_ROOM(k) if (n + (k) > 511) { parse_fail_set(R0S1_PARSE_TOO_LARGE); P->err = 1; break; }

/* GLON-VOCAB-1: `set 'name value` with a literal name is read as an explicit
 * assignment of the global (or, for a qualified name, of the vocabulary
 * member): the set-word name: undecorated, its global binding made now if
 * there is none.  Function locals are decorated, so none can catch it; a
 * parameter of an enclosing function with that name would, and is refused.
 * Answers 1 when it consumed `set 'name` (the value follows), 0 when the
 * words are something else (a computed set, a local or parameter named set). */
static int lx_explicit_set(parser_t *P, cell *out) {
    if (in_spec || cur_mode() == MODE_DATA) return 0;
    cell sid = word_id_of("set");
    int depth, slot; cell deco;
    if (lx_func_binding(sid, &depth, &slot, &deco) != 0) return 0;
    scan_t S = { P->s, P->pos };
    int st, ln;
    if (scan_next(&S, &st, &ln) != 'w' || P->s[st] != '\'' || ln < 2 || ln > 63) return 0;
    char name[64];
    memcpy(name, P->s + st + 1, (size_t)(ln - 1)); name[ln - 1] = 0;
    if (strncmp(name, "local/", 6) == 0 || name[ln - 2] == ':' || name[0] == ':') return 0;
    P->pos = S.pos;
    if (strchr(name, '/')) { *out = mk_set(lx_qualified(P, name, 's')); return 1; }
    cell id = word_id_of(name);
    if (lx_func_binding(id, &depth, &slot, &deco) == 1 || lx_func_binding(id, &depth, &slot, &deco) == 3) {
        lx_error("a parameter of an enclosing function has this name; set cannot reach the global", name);
        return 1;
    }
    cell gp = r0_untag(global_ctx), w = mk_word(id);
    int found = 0;
    for (cell k = 0; k < M[gp + CTX_COUNT] && !found; k++) if (M[gp + CTX_DATA + 2 * k] == w) found = 1;
    if (!found && M[gp + CTX_COUNT] < M[gp + CTX_CAP]) bind(global_ctx, w, R0_NONE);
    *out = mk_set(id);
    return 1;
}
/* GLON-VOCAB-1: the word before a nested block decides what it is */
static int lx_hint_of(cell prev) {
    if (r0_tag(prev) != T_WORD) return 0;
    const char *nm = syms[word_id(prev)];
    if (strcmp(nm, "do") == 0 || strcmp(nm, "reduce") == 0) return 1;
    if (strcmp(nm, "layout") == 0 || strcmp(nm, "panel") == 0 || strcmp(nm, "form") == 0 || strcmp(nm, "tabs") == 0) return 2;
    return 0;
}
static cell parse_block_in(parser_t *P, int is_body);
static cell parse_block(parser_t *P, int is_body);
/* after the keyword: NAME [ body ]  ->  tmp: do <body> */
static int lx_vocab_body(parser_t *P, cell *tmp, int *n) {
    skip_ws(P);
    int st = P->pos;
    while (P->s[P->pos] && !isspace((unsigned char)P->s[P->pos]) && P->s[P->pos] != '[' && P->s[P->pos] != ']') P->pos++;
    int ln = P->pos - st;
    char name[64];
    if (ln <= 0 || ln > 63) { lx_error("a vocabulary needs a name", "vocabulary"); return -1; }
    memcpy(name, P->s + st, (size_t)ln); name[ln] = 0;
    skip_ws(P);
    if (P->s[P->pos] != '[') { lx_error("a vocabulary needs a body block", name); return -1; }
    int v = lx_vocab_open(name, P->s, P->pos);
    if (v < 0 || parse_fail) return -1;
    if (vdepth >= 32) { lx_error("vocabularies nested too deeply", name); return -1; }
    vstack[vdepth++] = v;
    vocab_body_pending = 1;
    cell body = parse_block(P, 0);
    vdepth--;
    lx_vocab_close(v);
    if (P->err || parse_fail) return -1;
    tmp[(*n)++] = intern("do");
    tmp[(*n)++] = body;
    return 0;
}
static cell parse_block(parser_t *P, int is_body) {
    int mode;
    if (is_body || func_body_pending) { mode = MODE_FUNC; func_body_pending = 0; }
    else if (file_block_pending || vocab_body_pending) { mode = MODE_TOP; file_block_pending = 0; vocab_body_pending = 0; }
    else {
        int pm = cur_mode();
        if (pm == MODE_FUNC) mode = next_hint == 2 ? MODE_DATA : MODE_FUNC;
        else if (pm == MODE_TOP) mode = next_hint == 1 ? MODE_TOP : MODE_DATA;
        else mode = MODE_DATA;
    }
    next_hint = 0;
    if (mode_depth < MAX_MODE) mode_stack[mode_depth] = mode;
    mode_depth++;
    cell b = parse_block_in(P, is_body);
    mode_depth--;
    return b;
}
static cell parse_block_in(parser_t *P, int is_body) {
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
            PB_ROOM(2)
            tmp[n++] = intern("mk-string");
            tmp[n++] = parse_string_literal(P);
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
                PB_ROOM(2)                            /* func + spec at least */
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
                /* GLON-VOCAB-1: a literal spec block names parameters (never
                 * rewritten); a computed spec (func sp body) is an expression */
                skip_ws(P);
                int literal_spec = P->s[P->pos] == '[';
                if (literal_spec) { lex_depth = 0; in_spec++; }
                cell spec = parse_form(P);
                if (literal_spec) { in_spec--; lex_depth = save_lex_depth; }
                else if (r0_tag(spec) == T_BOUND) {
                    /* as before: a computed spec names no parameter slot */
                    int bd = (int)bound_depth(spec), bs = (int)(bound_payload(spec) % 16);
                    spec = mk_word(lex_names[lex_depth - 1 - bd][bs]);
                }
                /* Only a literal body block is this func's own lexical scope.
                 * A computed body (`func [] :b`, `func [] block-at rows i`) is
                 * an ordinary expression in the ENCLOSING scope; parsing it
                 * under the new scope would give its words wrong depths. */
                skip_ws(P);
                if (P->s[P->pos] != '[') {
                    tmp[n++] = spec;
                    continue;
                }
                PB_ROOM(2)                            /* spec + literal body */
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
                lx_func_locals(P->s, P->pos);            /* GLON-VOCAB-1: the body's locals */
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
                cell v;
                PB_ROOM(1)
                if (parse_masm(P, &v) < 0) break;
                tmp[n++] = v;
                continue;
            }
            if (len == 3 && strncmp(P->s + start, "set", 3) == 0) {   /* GLON-VOCAB-1 */
                cell sw;
                PB_ROOM(1)
                if (lx_explicit_set(P, &sw)) { tmp[n++] = sw; continue; }
            }
            /* GLON-VOCAB-1: `vocabulary NAME [ body ]` becomes `do [ body ]`,
             * its body's names resolved in the vocabulary */
            if (len == 10 && strncmp(P->s + start, "vocabulary", 10) == 0) {
                PB_ROOM(2)
                if (cur_mode() != MODE_TOP || lex_depth > 0) { lx_error("a vocabulary is declared at a top level (a source, a vocabulary or a top-level do)", "vocabulary"); P->err = 1; break; }
                if (lx_vocab_body(P, tmp, &n) < 0) { P->err = 1; break; }
                continue;
            }
            P->pos = start;
        }
        PB_ROOM(1)
        {
            next_hint = n > 0 ? lx_hint_of(tmp[n - 1]) : 0;
            cell form = parse_form(P);
            next_hint = 0;
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

static cell call_mark_value(void) { cell c = emit_call_fwd(); fw_mark_value[n_fw_mv++] = c; return c; }

#ifdef R0S1_GC_PROF
/* GLON-MEM-4 (diagnostic build only): the S1 address range of each collector
 * routine, each inlined object-start walk and each collector phase, recorded
 * in C while the code is emitted.  Reading asm_here() emits nothing: the
 * emitted code is identical to the normal build's.  view/tests/gcprof.h
 * samples the S1 IP (M[REG_IP]) against these ranges. */
#define R0S1_GCPROF_MAX 64
const char *r0s1_gcprof_name[R0S1_GCPROF_MAX];
cell r0s1_gcprof_lo[R0S1_GCPROF_MAX], r0s1_gcprof_hi[R0S1_GCPROF_MAX];
int r0s1_gcprof_n;
/* the M1 layout this translation unit was compiled with (the 1M/4M runtime
 * takes the root m1_layout.h, not experiments/ms1/include/m1_layout.h) */
const cell r0s1_gcprof_m1_task_table = M1_TASK_TABLE, r0s1_gcprof_m1_arena_base = M1_ARENA_BASE;
static void gcprof_add(const char *name, cell lo, cell hi) {
    if (r0s1_gcprof_n < R0S1_GCPROF_MAX) {
        r0s1_gcprof_name[r0s1_gcprof_n] = name;
        r0s1_gcprof_lo[r0s1_gcprof_n] = lo; r0s1_gcprof_hi[r0s1_gcprof_n] = hi;
        r0s1_gcprof_n++;
    }
}
#define GCPROF_ADD(name, lo, hi) gcprof_add(name, lo, hi)
#define GCPROF_EMIT(name, call) do { cell gcp_lo_ = asm_here(); call; gcprof_add(name, gcp_lo_, asm_here()); } while (0)
#else
#define GCPROF_ADD(name, lo, hi) ((void)0)
#define GCPROF_EMIT(name, call) call
#endif

/* WORKLIST-FULL: the mark worklist has no room (GC_WORKLIST_CAP entries).
 * Unreachable with the 64K map (r0_s1.h proves the bound at build time); this
 * is defence in depth for any other heap size or for RAW code that allocates
 * payloads smaller than 16 cells. It is a machine resource ceiling, not a SIN!
 * (no judge can catch it), handled like CONTEXT_FULL, with one extra step: a
 * collection is being abandoned half-way through its mark phase, so every mark
 * bit is cleared first. The heap is then exactly as it was before the
 * collection began (nothing was swept or freed), and a persistent session can
 * keep running. The walk is the sweep's: headers from GC_HEAP_BASE, never past
 * REG_HP or GC_HEAP_LIMIT, and it stops at a non-positive size rather than
 * loop. Then: record R0S1_HALT_GC_WORKLIST_FULL, dump and HALT, before any
 * write beyond the worklist. Uses GC_T6..GC_T8, which nothing reads after the
 * halt. */
static void emit_worklist_full_halt(void) {
    asm_lit(GC_HEAP_BASE); e_setc(GC_T6);                    /* addr */
    cell loop = asm_here();
    e_cell(GC_T6); asm_lit(REG_HP); asm_fetch(); asm_host(HOST_LT);
    cell j_end_hp = asm_zbranch_fwd();
    e_cell(GC_T6); asm_lit(GC_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_end_lim = asm_zbranch_fwd();
    e_cell(GC_T6); asm_fetch(); e_setc(GC_T7);               /* size */
    e_cell(GC_T7); asm_lit(0); asm_host(HOST_GT);
    cell j_end_bad = asm_zbranch_fwd();                      /* size <= 0: stop */
    e_cell(GC_T6); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T8); /* flags */
    e_cell(GC_T8); asm_lit(2); asm_host(HOST_DIV); asm_lit(2); asm_host(HOST_MOD);        /* mark bit */
    cell j_unmarked = asm_zbranch_fwd();
    e_cell(GC_T8); asm_lit(GC_FLAG_MARK); asm_host(HOST_SUB);
    e_cell(GC_T6); asm_lit(GC_HDR_FLAGS); asm_host(HOST_ADD); asm_store();               /* clear mark */
    asm_patch_here(j_unmarked);
    e_cell(GC_T6); e_cell(GC_T7); asm_host(HOST_ADD); e_setc(GC_T6);
    asm_branch(loop);
    asm_patch_here(j_end_hp);
    asm_patch_here(j_end_lim);
    asm_patch_here(j_end_bad);
    asm_lit(0); e_setc(GC_WL_SP);
    asm_lit(R0S1_HALT_GC_WORKLIST_FULL); e_setc(RV_HALTWHY);
    asm_host(HOST_DUMP); asm_halt();
}

/* MARK-PUSH: ( p -- ) mark collected object p (payload ptr) if unmarked, push
 * it on the explicit mark worklist. A full worklist fail-stops (above) before
 * the object is marked or anything is written beyond the worklist. */
static void emit_mark_push(void) {
    r_mark_push = asm_here();
    e_setc(GC_T1);                                   /* p */
    e_cell(GC_T1); asm_lit(GC_HDR_STRIDE - GC_HDR_FLAGS); asm_host(HOST_SUB); e_setc(GC_T2); /* flags addr */
    e_cell(GC_T2); asm_fetch(); e_setc(GC_T3);       /* flags */
    e_cell(GC_T3); asm_lit(2); asm_host(HOST_DIV); asm_lit(2); asm_host(HOST_MOD); /* mark bit */
    cell j_cont = asm_zbranch_fwd();                 /* not marked -> continue */
    asm_exit();                                      /* already marked */
    asm_patch_here(j_cont);
    e_cell(GC_WL_SP); asm_lit(GC_WORKLIST_CAP); asm_host(HOST_GE);   /* worklist full? */
    cell j_room = asm_zbranch_fwd();
    emit_worklist_full_halt();
    asm_patch_here(j_room);
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
 * frame chain is walked directly instead of going through the mark worklist.
 *
 * GLON-C3-2D: the chain is walked ITERATIVELY (newest frame first).  The
 * recursive walk used 2 return-stack cells per live frame (CALL + >R), so the
 * collector's depth grew with the recursion depth of the world being marked
 * (~182 cells at the 64K reference's deepest main recursion, run on whatever
 * stack the collection happened on, including a task's).  Now one >R holds p
 * across each MARK-VALUE call: constant depth.  The set of objects marked is
 * the same (every frame's CTX, each marked object pushed once by its mark
 * bit), and all roots are marked before the worklist drains, so the order
 * changes neither reachability nor the worklist's peak. */
static void emit_mark_frame(void) {
    r_mark_frame = asm_here();
    e_setc(GC_T1);                                   /* p */
    cell loop = asm_here();
    e_cell(GC_T1); asm_lit(0); asm_host(HOST_EQ);
    cell j_zero = asm_zbranch_fwd();
    asm_exit();                                      /* p == 0: end of chain */
    asm_patch_here(j_zero);
    e_cell(GC_T1); asm_toR();                        /* save p across the ctx mark */
    e_cell(GC_T1); asm_lit(FRAME_CTX);  asm_host(HOST_ADD); asm_fetch(); call_mark_value();
    asm_fromR();                                     /* p */
    asm_lit(FRAME_PREV); asm_host(HOST_ADD); asm_fetch(); e_setc(GC_T1);   /* p = p.prev */
    asm_branch(loop);
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
#ifdef R0S1_GC_CLOSURE_WALK_ALWAYS
    asm_lit(4); asm_host(HOST_DIV); asm_lit(8); asm_host(HOST_MOD);                     /* kind */
    asm_lit(GC_KIND_CLOSURE); asm_host(HOST_EQ);
    cell j4 = asm_zbranch_fwd();                     /* not a closure -> skip */
#else
    /* GLON-MEM-5: the kind test also requires the mark bit clear: flags/2
     * mod 16 is mark + 2*kind, so it equals 2*GC_KIND_CLOSURE only for "a
     * closure, not marked" (mark is 0 or 1; a negative flags word never
     * matches).  The same instructions, other literals: no extra code.
     * A candidate whose would-be flags have the mark bit set is skipped
     * before the object-start walk.  Its outcome is unchanged: a real
     * closure is already marked (mark_push would return at once), and a
     * non-object is rejected by the walk anyway.  So the walk is no longer
     * paid for every reference to a marked closure (GLON-MEM-4: 99.86% of
     * the walks).  Every candidate that is not skipped still takes the
     * walk, so nothing bypasses the object-start check.
     * -DR0S1_GC_CLOSURE_WALK_ALWAYS restores the kind-only test. */
    _Static_assert(GC_FLAG_ALLOC == 1 && GC_FLAG_MARK == 2 && GC_KIND_BLOCK < 8,
                   "flags = alloc | mark<<1 | kind<<2 with kind < 8 (the mark + kind test)");
    asm_lit(2); asm_host(HOST_DIV); asm_lit(16); asm_host(HOST_MOD);                    /* mark + 2*kind */
    asm_lit(2 * GC_KIND_CLOSURE); asm_host(HOST_EQ);
    cell j4 = asm_zbranch_fwd();                     /* not an unmarked closure -> skip */
#endif
    /* Object-start check (2026-11-09; r0_s1_gc_interior_tests.c). The kind
     * bits alone do not identify a closure: a STRING! holds one byte per cell,
     * so the inside of a string can carry "closure flags" (a byte 8-11) at
     * p - 15, and an INTEGER! n is the 16-aligned cell n * 16. Marking such a
     * p wrote the mark bit into a string byte. So p - 16 must be a real object
     * header: walk the object sizes from GC_HEAP_BASE (every cell below the
     * frontier belongs to exactly one allocated or free block, and the mark
     * phase changes only flags, never sizes) and skip p unless the walk lands
     * exactly on p - 16. Cost: O(objects) per closure-kind candidate. */
    asm_lit(GC_HEAP_BASE); e_setc(GC_T2);            /* a */
    e_cell(GC_T1); asm_lit(GC_HDR_STRIDE); asm_host(HOST_SUB); e_setc(GC_T3); /* h */
    e_cell(GC_T3); asm_lit(REG_HP); asm_fetch(); asm_host(HOST_LT);
    cell j_wfront = asm_zbranch_fwd();               /* h at/after the frontier */
    cell wl = asm_here();
    e_cell(GC_T2); e_cell(GC_T3); asm_host(HOST_LT);
    cell j_wdone = asm_zbranch_fwd();
    e_cell(GC_T2); asm_fetch(); asm_lit(0); asm_host(HOST_GT);
    cell j_wbad = asm_zbranch_fwd();                 /* size 0: give up */
    e_cell(GC_T2); e_cell(GC_T2); asm_fetch(); asm_host(HOST_ADD); e_setc(GC_T2);
    asm_branch(wl);
    GCPROF_ADD("object-start walk", wl, asm_here());
    asm_patch_here(j_wdone);
    e_cell(GC_T2); e_cell(GC_T3); asm_host(HOST_EQ);
    cell j_wno = asm_zbranch_fwd();                  /* not an object start */
    e_cell(GC_T1); asm_call(r_mark_push);
    asm_patch_here(j_wno);
    asm_patch_here(j_wbad);
    asm_patch_here(j_wfront);
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
    /* loader [R0S1_LOAD_BASE, R0S1_HEAP_LIMIT) */
    e_cell(GC_T1); asm_lit(R0S1_LOAD_BASE); asm_host(HOST_GE);
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
    /* p >= GC_HEAP_LIMIT: permanent loader block?  The worklist region
     * [GC_HEAP_LIMIT, R0S1_LOAD_BASE) is not a loader block. */
    asm_patch_here(j_not_managed);
    e_cell(GC_T1); asm_lit(R0S1_LOAD_BASE); asm_host(HOST_GE);
    cell j_loader_low = asm_zbranch_fwd();                  /* p < LOAD_BASE -> corrupt */
    e_cell(GC_T1); asm_lit(R0S1_HEAP_LIMIT); asm_host(HOST_LT);
    cell j_not_loader = asm_zbranch_fwd();                  /* p >= R0S1_HEAP_LIMIT -> corrupt */
    asm_exit();                                             /* loader block: no children */
    asm_patch_here(j_not_loader);
    asm_patch_here(j_loader_low);
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
/* Collector-entry return-stack guard (GLON-C3-2A, see R0S1_GC_RS_GUARD): a
 * collection needs more return stack than an evaluation step (its marking
 * calls), so it checks its own room on entry, against the floor of the world
 * SP is in -- derived from SP here, because a task switch inside a step can
 * start a collection before the next step entry updates RV_RSLIMIT.  Data
 * stack only; balanced, so SP0/RP0 below are captured unchanged.  Exhaustion
 * branches (patched after the error core is emitted) to STACK-EXHAUSTED. */
static cell gc_exhausted_patch;
static void emit_collect_rs_guard(void) {
    asm_lit(REG_SP); asm_fetch(); asm_dup(); asm_lit(R0S1_DS_INIT); asm_host(HOST_GT);
    cell j_main = asm_zbranch_fwd();
    asm_lit(M1_ARENA_BASE); asm_host(HOST_SUB);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_DIV);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_MUL);
    asm_lit(M1_ARENA_BASE + M1_DS_OFF); asm_host(HOST_ADD);      /* task RS floor */
    cell j_cmp = asm_branch_fwd();
    asm_patch_here(j_main);
    asm_drop(); asm_lit(R0S1_DS_INIT);                           /* main RS floor */
    asm_patch_here(j_cmp);
    asm_lit(REG_RP); asm_fetch(); asm_lit(R0S1_GC_RS_GUARD); asm_host(HOST_SUB);
    asm_host(HOST_GT);                                           /* floor > RP - guard ? */
    cell j_ok = asm_zbranch_fwd();
    asm_lit(R0S1_HALT_RS_EXHAUSTED); gc_exhausted_patch = asm_branch_fwd();
    asm_patch_here(j_ok);
}

static void emit_collect(void) {
    r_collect = asm_here();
    emit_collect_rs_guard();                         /* GLON-C3-2A */
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
    /* GLON-C3-2D: the parked main world's own roots.  Every scheduler
     * (task.glon, the M1 test library, the shop demos) saves the whole
     * record -- IP, SP, RP, CUR, END, CTX (+5), BLK, FRAME (+7) -- before it
     * dispatches a task, and restores it only when control returns to main,
     * so while a task runs +5/+7 are main's current context and frame chain.
     * Main's locals live in its stack contexts, reached only through them;
     * the two scans above see main's data stack and raw closure pointers on
     * its return stack, not the tagged values in its contexts, so without
     * these roots a collection inside a task freed objects main still used
     * (inherited from W5.1).  Validity: the record must describe the MAIN
     * world (saved SP at or below DS_INIT -- not a task's, as a run-tasks
     * called inside a task would leave); otherwise fail-stop before anything
     * is traversed.  Before the first run-tasks the record is the init seed
     * (SP = DS_INIT, CTX = FRAME = 0: an integer and an empty chain).
     * MARK-VALUE classifies the context (an out-of-range one fail-stops);
     * MARK-FRAME's walk is iterative: constant depth on the task's stack. */
    e_cell(M1_SCHED_REC + 1); asm_lit(R0S1_DS_INIT); asm_host(HOST_GT);
    cell j_sched_main = asm_zbranch_fwd();
    asm_host(HOST_DUMP); asm_halt();                            /* not a main-world record */
    asm_patch_here(j_sched_main);
    e_cell(M1_SCHED_REC + 5); asm_call(r_mark_value);
    e_cell(M1_SCHED_REC + 7); asm_call(r_mark_frame);
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
    /* GLON-C3-2 (W5.1 H2D): a RUNNABLE task's response value is a root, marked
     * by the same r_mark_value as TREC_CTX.  task.glon keeps the field none (an
     * immediate) outside a task's life, so this marks nothing for records that
     * never set it.  -DR0S1_NO_TASK_RESP_ROOT omits it: a test-only negative
     * control (tests/core-c3/task_tests.c), never a supported build. */
#ifndef R0S1_NO_TASK_RESP_ROOT
    e_cell(GC_C4); asm_lit(TREC_RESP); asm_host(HOST_ADD); asm_fetch(); asm_call(r_mark_value);
#endif
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
    GCPROF_ADD("collect: roots", r_collect, drain);
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
    GCPROF_ADD("collect: drain loop", drain, asm_here());
#ifdef R0S1_GC_PROF
    cell gcp_sweep = asm_here();
#endif
    asm_lit(GC_HEAP_BASE); e_setc(GC_T6);            /* addr */
    asm_lit(0); e_setc(GC_C6);                       /* prev_free */
    /* GLON-MEM-2: the hint is replaced at the sweep's first free run (below);
     * with no free run there is no free block at all, and the hint kept (at
     * most the frontier) is still a lower bound */
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
#ifndef R0S1_ALLOC_SCAN_FROM_BASE
    /* GLON-MEM-2: the sweep's first free run (none counted yet) is the
     * lowest free block */
    e_cell(GC_FREE_BLOCKS);
    cell j_hfirst = asm_zbranch_fwd();               /* count 0 -> first run */
    cell j_hnot = asm_branch_fwd();
    asm_patch_here(j_hfirst);
    e_cell(GC_T6); e_setc(GC_FREE_HINT);
    asm_patch_here(j_hnot);
#endif
    e_cell(GC_T6); e_setc(GC_C6);                    /* prev_free = addr */
    e_cell(GC_FREE_BLOCKS); asm_lit(1); asm_host(HOST_ADD); e_setc(GC_FREE_BLOCKS);
    asm_patch_here(fw_live_next);
    asm_patch_here(fw_coal_next);
    e_cell(GC_T6); e_cell(GC_T7); asm_host(HOST_ADD); e_setc(GC_T6);
    asm_branch(sloop);
    asm_patch_here(j_sdone);

    e_cell(GC_FREE_CELLS); e_setc(GC_LAST_RECLAM);
    asm_exit();
    GCPROF_ADD("collect: sweep", gcp_sweep, asm_here());
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
#ifdef R0S1_ALLOC_SCAN_FROM_BASE
    asm_lit(GC_HEAP_BASE); e_setc(GC_T2);            /* addr */
#else
    /* GLON-MEM-2: start at the free-block hint (no free block lies below it).
     * A hint at or above the frontier scans nothing and bumps, and the bump
     * then sets it to the new frontier (below), so a hint left above a lowered
     * frontier (only the D1 tooling's raw REG_HP writes; every C-side write
     * resets it) never lands inside a block.  (A hint below GC_HEAP_BASE only
     * arises with a frontier below it -- the 1M/4M non-persistent re-base --
     * where neither scan finds a free block.) */
#ifdef R0S1_ALLOC_HINT_MEM2
    e_cell(GC_FREE_HINT); e_setc(GC_T2);             /* addr = hint (GLON-MEM-2 rule, for comparison) */
#else
    /* addr = hint; GLON-MEM-3: T8 = the hint (nonzero) until a free block
     * is passed */
    e_cell(GC_FREE_HINT); asm_dup(); e_setc(GC_T2); e_setc(GC_T8);
#endif
#endif
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
#if !defined(R0S1_ALLOC_SCAN_FROM_BASE) && !defined(R0S1_ALLOC_HINT_MEM2)
    asm_lit(0); e_setc(GC_T8);                       /* GLON-MEM-3: a free block passed */
#endif
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
#ifndef R0S1_ALLOC_SCAN_FROM_BASE
#ifdef R0S1_ALLOC_HINT_MEM2
    /* no free block below the old frontier: none below the new one either
     * (>=: a hint above the frontier is brought back to it) */
    e_cell(GC_FREE_HINT); e_cell(GC_T2); asm_host(HOST_GE);
#else
    /* GLON-MEM-3: the scan passed no free block (everything from the hint
     * to the old frontier is live, or the hint was at or above it): nothing
     * free lies below the new frontier */
    e_cell(GC_T8);
#endif
    cell j_hbump = asm_zbranch_fwd();
    asm_lit(REG_HP); asm_fetch(); e_setc(GC_FREE_HINT);
    asm_patch_here(j_hbump);
#endif
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
#ifndef R0S1_ALLOC_SCAN_FROM_BASE
    /* GLON-MEM-3: no free block was passed on the way (everything from the
     * hint to here is live): nothing free lies below the end of the block
     * taken (a split's remainder starts there; its final size is at M[addr]).
     * With a free block passed, the hint stays: still a bound */
#ifdef R0S1_ALLOC_HINT_MEM2
    e_cell(GC_T2); e_cell(GC_FREE_HINT); asm_host(HOST_EQ);   /* GLON-MEM-2: only a block at the hint */
#else
    e_cell(GC_T8);
#endif
    cell j_passed = asm_zbranch_fwd();               /* 0: a free block was passed */
    e_cell(GC_T2); e_cell(GC_T2); asm_fetch(); asm_host(HOST_ADD); e_setc(GC_FREE_HINT);
    asm_patch_here(j_passed);
#endif
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
    /* GLON-C3-2A: a caught raise is not a fail-stop; drop a reason recorded
     * by a task's stack guard (TASK-EXHAUSTED), so a later halt never reports
     * it.  Nothing else records a reason before raising. */
    asm_lit(R0S1_HALT_NONE); e_setc(RV_HALTWHY);
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

/* STACK-EXHAUSTED (GLON-C3-2A): ( reason -- ) the stack guard found the
 * current world's data or return stack exhausted (reason R0S1_HALT_DS_- or
 * R0S1_HALT_RS_EXHAUSTED, recorded in RV_HALTWHY).  Main world: the fatal
 * fail-stop it always was.  M1 task world (SP above DS_INIT): raise SIN!
 * [machine stack-exhausted <reason>], so the task's judge (task-run) ends that
 * task alone and the scheduler and every other task carry on.  The raise uses
 * three data-stack cells (within the guard's headroom) and no return stack
 * before RAISE-UNWIND discards both stacks down to the judge frame; the SIN!
 * is allocated only at the landing, with the judge's stacks.  Uncaught (a task
 * with no judge frame), RAISE-UNWIND halts with the reason still recorded. */
static cell r_stack_exhausted;
static void emit_stack_exhausted_routine(void) {
    r_stack_exhausted = asm_here();
    e_setc(RV_HALTWHY);
    asm_lit(REG_SP); asm_fetch(); asm_lit(R0S1_DS_INIT); asm_host(HOST_GT);
    cell j_main = asm_zbranch_fwd();
    emit_word_lit("machine"); emit_word_lit("stack-exhausted");
    e_cell(RV_HALTWHY); asm_lit(16); asm_host(HOST_MUL);   /* the reason, as a Glon integer */
    asm_branch(r_raise3);
    asm_patch_here(j_main);
    asm_host(HOST_DUMP); asm_halt();
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
    emit_stack_exhausted_routine();
}

/* Data-stack bounds check (see R0S1_DS_GUARD), emitted at SUBEXPR entry.
 *   main world, SP <= DS_INIT: SP must be >= RV_DSLIM (end of code + guard);
 *   M1 task world, SP in the task arena: with base = the slot's first cell,
 *     base + guard <= SP <= base + M1_DS_OFF;
 *   anything else (DS_INIT < SP < the arena, or past it): underflow.
 * The main path costs 10 S1 instructions. Uses the data stack and the
 * guard's own scratch cell RV_DSBASE only (no call, nothing else live).
 * A violation is a fatal fail-stop, before the step pushes anything:
 * R0S1_HALT_DS_EXHAUSTED below the floor, R0S1_HALT_DS_UNDERFLOW above the
 * top.  GLON-C3-2A: the same world test sets RV_RSLIMIT (the return stack's
 * floor plus its guard) and the return stack is checked after the data
 * stack; exhaustion of either goes to STACK-EXHAUSTED (fatal in the main
 * world, a task failure in an M1 task). */
static void emit_ds_guard(void) {
    asm_lit(REG_SP); asm_fetch();                         /* sp */
    asm_dup(); asm_lit(R0S1_DS_INIT); asm_host(HOST_GT);  /* sp (sp > DS_INIT) */
    cell j_main = asm_zbranch_fwd();
    /* not the main data stack: a task's, or an underflow */
    asm_dup(); asm_lit(M1_ARENA_BASE); asm_host(HOST_LT);
    cell j_inarena = asm_zbranch_fwd();
    cell j_under1 = asm_branch_fwd();                     /* DS_INIT < sp < arena */
    asm_patch_here(j_inarena);
    asm_dup(); asm_lit(M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS); asm_host(HOST_LT);
    cell j_past = asm_zbranch_fwd();                      /* sp past the arena */
    asm_dup(); asm_lit(M1_ARENA_BASE); asm_host(HOST_SUB);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_DIV);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_MUL);
    asm_lit(M1_ARENA_BASE); asm_host(HOST_ADD); e_setc(RV_DSBASE);  /* sp */
    e_cell(RV_DSBASE); asm_lit(M1_DS_OFF + R0S1_RS_GUARD); asm_host(HOST_ADD);
    e_setc(RV_RSLIMIT);                                   /* task RS floor + guard */
    asm_dup(); e_cell(RV_DSBASE); asm_lit(M1_DS_OFF); asm_host(HOST_ADD);
    asm_host(HOST_GT);                                    /* sp (sp > base + DS_OFF) */
    cell j_tasktop = asm_zbranch_fwd();
    cell j_under2 = asm_branch_fwd();
    asm_patch_here(j_tasktop);
    e_cell(RV_DSBASE); asm_lit(R0S1_DS_GUARD); asm_host(HOST_ADD); /* sp floor */
    cell j_cmp = asm_branch_fwd();
    asm_patch_here(j_main);
    asm_lit(R0S1_DS_INIT + R0S1_RS_GUARD); e_setc(RV_RSLIMIT);   /* main RS floor + guard */
    e_cell(RV_DSLIM);                                     /* sp floor */
    asm_patch_here(j_cmp);
    asm_host(HOST_LT);
    cell j_ok = asm_zbranch_fwd();
    asm_lit(R0S1_HALT_DS_EXHAUSTED); asm_branch(r_stack_exhausted);   /* data-stack exhaustion */
    asm_patch_here(j_under1);
    asm_patch_here(j_past);
    asm_patch_here(j_under2);
    asm_lit(R0S1_HALT_DS_UNDERFLOW); e_setc(RV_HALTWHY);
    asm_host(HOST_DUMP); asm_halt();                      /* data-stack underflow */
    asm_patch_here(j_ok);
    /* GLON-C3-2A: the return stack, against the world's limit set above */
    asm_lit(REG_RP); asm_fetch(); e_cell(RV_RSLIMIT); asm_host(HOST_LT);
    cell j_rs = asm_zbranch_fwd();
    asm_lit(R0S1_HALT_RS_EXHAUSTED); asm_branch(r_stack_exhausted);   /* return-stack exhaustion */
    asm_patch_here(j_rs);
}

/* ( cell -- ) fail-stop as data-stack underflow unless `cell` is below the
 * top of the data stack SP is in: DS_INIT for the main world, base +
 * M1_DS_OFF for an M1 task (an SP elsewhere is itself an underflow). Uses
 * the data stack only. */
static void emit_ds_top_check(void) {
    asm_lit(REG_SP); asm_fetch();                         /* c sp */
    asm_dup(); asm_lit(R0S1_DS_INIT); asm_host(HOST_GT);
    cell j_main = asm_zbranch_fwd();
    asm_dup(); asm_lit(M1_ARENA_BASE); asm_host(HOST_LT);
    cell j_inarena = asm_zbranch_fwd();
    asm_drop(); asm_drop();
    cell j_under1 = asm_branch_fwd();
    asm_patch_here(j_inarena);
    asm_dup(); asm_lit(M1_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS); asm_host(HOST_LT);
    cell j_inside = asm_zbranch_fwd();
    asm_lit(M1_ARENA_BASE); asm_host(HOST_SUB);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_DIV);
    asm_lit(M1_TASK_CELLS); asm_host(HOST_MUL);
    asm_lit(M1_ARENA_BASE + M1_DS_OFF); asm_host(HOST_ADD); /* c top */
    cell j_cmp = asm_branch_fwd();
    asm_patch_here(j_inside);
    asm_drop(); asm_drop();
    cell j_under2 = asm_branch_fwd();
    asm_patch_here(j_main);
    asm_drop(); asm_lit(R0S1_DS_INIT);                    /* c top */
    asm_patch_here(j_cmp);
    asm_host(HOST_LT);                                    /* c < top */
    cell j_bad = asm_zbranch_fwd();
    cell j_ok = asm_branch_fwd();
    asm_patch_here(j_bad);
    asm_patch_here(j_under1);
    asm_patch_here(j_under2);
    asm_lit(R0S1_HALT_DS_UNDERFLOW); e_setc(RV_HALTWHY);
    asm_host(HOST_DUMP); asm_halt();                      /* data-stack underflow */
    asm_patch_here(j_ok);
}

/* the main data-stack floor follows the end of emitted code */
static void ds_floor_update(void) { M[RV_DSLIM] = asm_here() + R0S1_DS_GUARD; }

static void emit_gc(void) {
    GCPROF_EMIT("mark_push", emit_mark_push());
    GCPROF_EMIT("trace_ctx", emit_trace_ctx());
    GCPROF_EMIT("trace_closure", emit_trace_closure());
    GCPROF_EMIT("trace_user", emit_trace_user());
    GCPROF_EMIT("trace_string", emit_trace_string());
    GCPROF_EMIT("trace_block", emit_trace_block());
    GCPROF_EMIT("mark_frame", emit_mark_frame());
    GCPROF_EMIT("mark_value", emit_mark_value());
    GCPROF_EMIT("scan_values", emit_scan_values());
    GCPROF_EMIT("scan_closures", emit_scan_closures());
    GCPROF_EMIT("collect", emit_collect());
    GCPROF_EMIT("alloc", emit_alloc());
    for (int i = 0; i < n_fw_mv; i++) s1_set_mem(fw_mark_value[i], r_mark_value);
}

/* ========================== evaluator build ============================ */

static cell r_reduce, r_append, r_mkctx, r_mkclosure;
static cell r_load_lex, r_hash_insert, r_build_hash;
#ifndef R0S1_SET_LINEAR
static cell r_find_addr;   /* INTENT-7: LOOKUP returning the binding's value-cell address */
#endif
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
#ifndef R0S1_SET_LINEAR
/* INTENT-7: at a binding found, LOOKUP fetches its value; FIND-ADDR (the same
 * walk, entered with RV_FINDMODE = 1) leaves the value cell's address */
static void emit_fetch_unless_find(void) {
    e_cell(RV_FINDMODE);
    cell j_value = asm_zbranch_fwd();            /* mode 0: the value */
    cell j_addr = asm_branch_fwd();              /* mode 1: the address as it is */
    asm_patch_here(j_value);
    asm_fetch();
    asm_patch_here(j_addr);
}
#define EMIT_LOOKUP_FETCH() emit_fetch_unless_find()
#else
#define EMIT_LOOKUP_FETCH() asm_fetch()
#endif
static void emit_lookup(void) {
#ifndef R0S1_SET_LINEAR
    /* FIND-ADDR: (word -- addr | -1 if unbound).  The same chain walk as
     * LOOKUP -- the hash index for a hashed context, else the ordered linear
     * scan -- leaving RV_T2 = the payload of the context it was found in */
    r_find_addr = asm_here();
    asm_lit(1); e_setc(RV_FINDMODE);
    cell fw_body = asm_branch_fwd();
#endif
    r_lookup = asm_here();
#ifndef R0S1_SET_LINEAR
    asm_lit(0); e_setc(RV_FINDMODE);
    asm_patch_here(fw_body);
#endif
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
    asm_dup(); asm_lit(R0S1_HASH_RADIX); asm_host(HOST_DIV);                    /* [ctx, e, key] */
    e_cell(RV_T4); asm_host(HOST_EQ);                                           /* [ctx, e, key==id] */
    cell j_hcollide = asm_zbranch_fwd();
    asm_lit(R0S1_HASH_RADIX); asm_host(HOST_MOD);                               /* [ctx, slot] (slot = e % radix) */
    e_setc(RV_T5);                                                              /* [ctx] */
#ifdef R0_S1_PROFILE
    pf_incr(PF_HASH_HITS);
#endif
    e_cell(RV_T2); asm_lit(CTX_DATA + 1); asm_host(HOST_ADD);
    e_cell(RV_T5); asm_lit(2); asm_host(HOST_MUL); asm_host(HOST_ADD);
    EMIT_LOOKUP_FETCH();                                                        /* [ctx, value] */
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
    EMIT_LOOKUP_FETCH();
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
    e_cell(RV_T5); asm_lit(R0S1_HASH_RADIX); asm_host(HOST_MUL); e_cell(RV_T3); asm_host(HOST_ADD); /* id*radix+slot */
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

/* the binding a set-word makes in RV_CTX when the word is bound nowhere in
 * the chain (shared by both forms of SET below); the value is in RV_T6 */
static void emit_set_append(void) {
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
}

#ifndef R0S1_SET_LINEAR
/* SET: ( value -- value )  assign RV_WORD.  INTENT-7: the binding is found
 * by FIND-ADDR -- the same walk as LOOKUP, through each context's hash index
 * when it has one -- instead of a linear scan of every context in the chain.
 * A set-word for a new local in a function defined at top level used to scan
 * the whole global context (940 bindings in the membership application, 51%
 * of its S1 time).  Found anywhere: stored there (after the escape-law store
 * check, RV_T2 = that context).  Bound nowhere: a new binding in RV_CTX, as
 * before.  -DR0S1_SET_LINEAR restores the linear scan. */
static void emit_set(void) {
    r_set = asm_here();
    e_cell(RV_WORD); asm_call(r_find_addr);  /* [value, addr | -1] */
    asm_dup(); asm_lit(-1); asm_host(HOST_EQ);
    cell j_found = asm_zbranch_fwd();
    asm_drop();                              /* [value] */
    e_peek(); e_setc(RV_T6);
    emit_set_append();
    asm_patch_here(j_found);
    e_setc(RV_T4);                           /* [value]; RV_T4 = the value cell */
    e_peek(); e_setc(RV_T6);
    emit_esc_store_check();
    e_cell(RV_T6); e_cell(RV_T4); asm_store();
    asm_exit();
}
#else
static void emit_set(void) {
    r_set = asm_here();
    e_peek(); e_setc(RV_T6);
    e_cell(RV_CTX);
    cell outer = asm_here();
    e_dup(); asm_lit(R0_NONE); asm_host(HOST_EQ);
    cell j_continue = asm_zbranch_fwd();
    asm_drop();
    emit_set_append();
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

#endif

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
    /* GLON-C3-2A: the return stack is checked at every SUBEXPR entry (R0S1_RS_GUARD) */
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
    /* GLON-C3-2A: the invocation's return-stack room is checked at the
     * SUBEXPR entry that leads here (R0S1_RS_GUARD covers the frame and the
     * calls around it); the former RP - 96 check here is subsumed. */
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
    /* The result set [v1 .. vn, n] must lie inside the data stack the word
     * returned on: a word that declares more results than it left (ARITY n
     * after consuming its arguments) hands back cells above the stack's top,
     * the bottom of a return stack, and the caller's next push writes there.
     * Fail-stop as data-stack underflow first. (The word may return on
     * another stack: masm code may switch stacks, as the M1 tasks do.) */
    asm_lit(REG_SP); asm_fetch();                       /* sp */
    asm_dup(); asm_fetch(); asm_lit(16); asm_host(HOST_DIV);
    asm_host(HOST_ADD);                                 /* sp + n: the last result cell */
    emit_ds_top_check();
    asm_exit();                                         /* return the result set */
}

/* SUBEXPR: evaluate one sub-expression at RV_CUR, advancing it */
static void emit_subexpr(void) {
    r_subexpr = asm_here();
    cell fw_disp_word, fw_disp_bound;   /* forward branches to the shared value dispatch */
    emit_ds_guard();                    /* fail-stop before the data stack reaches code */
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
    M[GC_LOADER_HP] = R0S1_LOAD_BASE;
    nsyms = 0;
    nvocabs = 0; vm_nlog = 0; memset(vm_used, 0, sizeof vm_used); native_bits_ready = 0;   /* GLON-VOCAB-1 */
    n_subexpr = 0; n_block_eval = 0;
    n_invoke = 0;
    n_fw_mv = 0;
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
    GCPROF_EMIT("error_core", emit_error_core());
    s1_set_mem(gc_exhausted_patch, r_stack_exhausted);   /* GLON-C3-2A: the collector guard's target */
    GCPROF_EMIT("reduce", emit_reduce());
    GCPROF_EMIT("lookup", emit_lookup());
    GCPROF_EMIT("load_lex", emit_load_lex());
    GCPROF_EMIT("hash_insert", emit_hash_insert());
    GCPROF_EMIT("build_hash", emit_build_hash());
    GCPROF_EMIT("set", emit_set());
    GCPROF_EMIT("append", emit_append());
    GCPROF_EMIT("mkctx", emit_mkctx());
    GCPROF_EMIT("mkclosure", emit_mkclosure());
    GCPROF_EMIT("binding_ctx", emit_binding_ctx());
    GCPROF_EMIT("values", emit_values());
    GCPROF_EMIT("reduce_block", emit_reduce_block());
    GCPROF_EMIT("run_block", emit_run_block());
    GCPROF_EMIT("native", emit_native());
    GCPROF_EMIT("invoke_closure", emit_invoke_closure());
    GCPROF_EMIT("return", emit_return());
    GCPROF_EMIT("invoke_raw", emit_invoke_raw());
    GCPROF_EMIT("subexpr", emit_subexpr());
    GCPROF_EMIT("block_eval", emit_block_eval());
    GCPROF_EMIT("main", emit_main());
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
    ds_floor_update();

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
    global_ctx = make_context(R0_NONE, R0S1_GLOBAL_CAP);
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
    for (int i = 0; i < M1_MAX_TASKS; i++) {
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
        /* GLON-C3-2: not blocked, generation 0, no response (none, an immediate) */
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_BLOCKED] = 0;
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_GEN] = 0;
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_RESP] = R0_NONE;
    }
    M[M1_SCHED_REC + 1] = R0S1_DS_INIT;   /* scheduler DS: empty range */
    M[M1_SCHED_REC + 2] = R0S1_RS_INIT;   /* scheduler RS: empty range */
    /* GLON-C3-2D: the parked main world's saved CTX/FRAME are collector
     * roots while a task runs; until the first run-tasks saves them they
     * hold 0 (an integer: no context, an empty frame chain), never a
     * previous session's values. */
    for (int i = 3; i <= 7; i++) M[M1_SCHED_REC + i] = 0;
    M[GC_COLLECT_CNT] = 0;
    M[GC_LAST_RECLAM] = 0;
    /* Seed the managed-heap frontier. The non-persistent r0_s1_run path resets
     * REG_HP via s1_reset(), but the G1E load-on-demand path calls
     * r0_s1_run_persistent FIRST (bootstrap), which preserves REG_HP as-is; it
     * must therefore start at the empty-heap base, not an uninitialised cell. */
    M[REG_HP] = GC_HEAP_BASE;
    M[GC_FREE_HINT] = GC_HEAP_BASE;     /* GLON-MEM-2: an empty heap */

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
    if (P->s[P->pos] == '[') { file_block_pending = 1; return parse_block(P, 0); }
    cell tmp[512]; int n = 0;
    while (P->s[P->pos] && !P->err && !parse_fail) {
        skip_ws(P);
        if (!P->s[P->pos]) break;
        if (P->s[P->pos] == '"') {
            PB_ROOM(2)
            tmp[n++] = intern("mk-string");
            tmp[n++] = parse_string_literal(P);
            continue;
        }
        {   /* `masm`/`raw`: the same definition form as inside a block */
            char c = P->s[P->pos];
            if (c != '[' && c != '-' && !(c >= '0' && c <= '9')) {
                int start = P->pos;
                while (P->s[P->pos] && !isspace((unsigned char)P->s[P->pos])
                       && P->s[P->pos] != '[' && P->s[P->pos] != ']') P->pos++;
                int len = P->pos - start;
                if ((len == 3 && strncmp(P->s + start, "raw", 3) == 0) ||
                    (len == 4 && strncmp(P->s + start, "masm", 4) == 0)) {
                    cell v;
                    PB_ROOM(1)
                    if (parse_masm(P, &v) < 0) break;
                    tmp[n++] = v;
                    continue;
                }
                if (len == 10 && strncmp(P->s + start, "vocabulary", 10) == 0) {   /* GLON-VOCAB-1 */
                    PB_ROOM(2)
                    if (lx_vocab_body(P, tmp, &n) < 0) { P->err = 1; break; }
                    continue;
                }
                if (len == 3 && strncmp(P->s + start, "set", 3) == 0) {     /* GLON-VOCAB-1 */
                    cell sw;
                    PB_ROOM(1)
                    if (lx_explicit_set(P, &sw)) { tmp[n++] = sw; continue; }
                }
                /* GLON-VOCAB-1: a top-level func of an unwrapped source keeps
                 * the forms it had (no site, no T_BOUND), with a scope that
                 * knows its parameters and locals by name */
                if (len == 4 && strncmp(P->s + start, "func", 4) == 0) {
                    PB_ROOM(3)
                    tmp[n++] = intern("func");
                    skip_ws(P);
                    int literal_spec = P->s[P->pos] == '[';
                    if (literal_spec) in_spec++;
                    cell spec = parse_form(P);
                    if (literal_spec) in_spec--;
                    tmp[n++] = spec;
                    skip_ws(P);
                    if (P->err || P->s[P->pos] != '[') continue;
                    lex_push();
                    lex_pseudo[lex_depth - 1] = 1;
                    if (r0_tag(spec) == T_BLOCK) {
                        cell sp = r0_untag(spec);
                        for (int i = (int)M[sp] - 1; i >= 0; i--) {
                            cell e = M[sp + BLK_DATA + i];
                            if (r0_tag(e) == T_WORD) lex_add(word_id(e));
                        }
                    }
                    lx_func_locals(P->s, P->pos);
                    func_body_pending = 1;
                    tmp[n++] = parse_block(P, 0);
                    lex_pop();
                    continue;
                }
                P->pos = start;
            }
        }
        PB_ROOM(1)
        next_hint = n > 0 ? lx_hint_of(tmp[n - 1]) : 0;
        tmp[n++] = parse_form(P);
        next_hint = 0;
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
 * The return value is then R0_NONE and must not be run. A failed source
 * emits no code either: its masm blocks are assembled only after the whole
 * source has parsed (see raw_pending), because the frozen assembler's cursor
 * cannot move back. */
/* Assemble the masm blocks of a source that parsed (see raw_pending). */
static void raw_emit_pending(const char *src) {
    for (int i = 0; i < raw_npending; i++) {
        parser_t Q; Q.s = src; Q.pos = raw_pending[i].pos; Q.err = 0;
        int ntok = masm_read_block(&Q);
        cell entry = (ntok >= 0 && !Q.err) ? assemble_raw(masm_toks, ntok) : -1;
        if (entry != raw_pending[i].at || asm_here() - entry != raw_pending[i].need) {
            /* the block did not land where its callable says: a runtime bug */
            fprintf(stderr, "r0_s1: internal error: masm block %d emitted at %ld (%ld cells); "
                            "%ld (%ld cells) were recorded\n", i, (long)entry,
                    (long)(asm_here() - entry), (long)raw_pending[i].at, (long)raw_pending[i].need);
            abort();
        }
    }
    raw_npending = 0; raw_pending_cells = 0;
    ds_floor_update();
}

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
    raw_npending = 0; raw_pending_cells = 0;
    /* GLON-VOCAB-1: the source's identity (for member ownership), its own
     * top-level names; a fresh scope state */
    {
        unsigned hsh = 2166136261u;
        for (const char *c = src; *c; c++) hsh = (hsh ^ (unsigned char)*c) * 16777619u;
        cur_src = hsh ? hsh : 1;
        cur_serial++;
    }
    int save_vm = vm_nlog, save_nv = nvocabs;
    mode_depth = 0; vdepth = 0; in_spec = 0; next_hint = 0; file_block_pending = 0; vocab_body_pending = 0; func_body_pending = 0;
    memset(file_top, 0, sizeof file_top);
    memset(earlier_data, 0, sizeof earlier_data);
    {
        cell gp = r0_untag(global_ctx);
        for (cell k = 0; k < M[gp + CTX_COUNT]; k++) {
            cell w = M[gp + CTX_DATA + 2 * k], val = M[gp + CTX_DATA + 2 * k + 1];
            int t = (int)r0_tag(val);
            if (t != T_CLOSURE && t != T_NATIVE && t != T_RAW && word_id(w) < R0S1_SYM_CAP) BIT_SET(earlier_data, word_id(w));
        }
    }
    {
        int i = 0;
        while (src[i] && isspace((unsigned char)src[i])) i++;
        scan_t S = { src, src[i] == '[' ? i + 1 : 0 };
        prescan_top(&S, src[i] == '[', NULL, top_add_file);
    }
    cell b = parse_program(&P);
    if (!P.err && !parse_fail) {
        raw_emit_pending(src);
        return b;
    }
    raw_npending = 0; raw_pending_cells = 0;   /* a failed source emits no code */
    vm_rollback(save_vm, save_nv);             /* GLON-VOCAB-1: nor any member */
    mode_depth = 0; vdepth = 0; in_spec = 0;
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
 * the world the run ended in (main DS [end of emitted code, DS_INIT], main RS
 * [DS_INIT, RS_INIT], or a task DS [base, base+M1_DS_OFF] / task RS
 * [base+M1_DS_OFF, base+M1_RS_OFF]). A violation is reported immediately, with
 * the offending register state, not a later GC or unrelated failure. */
static void r0_s1_stack_sentry(void) {
    cell sp = s1_mem(REG_SP), rp = s1_mem(REG_RP);
    /* The code boundary is the CURRENT end of emitted code: the runtime's own
     * code plus every masm library assembled since (code_end is only the
     * runtime's). RV_SPMIN is the deepest SP the run recorded (at every
     * closure invocation; reset at each run), so growth into code that
     * shrank back before the end is detected too. */
    cell code_top = asm_here();
    const char *what = NULL;
    if (sp <= R0S1_DS_INIT) {
        /* main world */
        if (sp < code_top)              what = "main data-stack overflow (SP < code)";
        else if (s1_mem(RV_SPMIN) < code_top)
                                        what = "main data-stack overflow during the run (deepest SP < code)";
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
    ds_floor_update();         /* code may have grown since (masm, compiled code) */
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
    /* GLON-MEM-2: a re-based (or seeded) frontier: everything below it is
     * allocated; a preserved one keeps its hint */
    if (!preserve_hp || g_seed_datatypes) M[GC_FREE_HINT] = M[REG_HP];
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
    ds_floor_update();         /* code may have grown since (masm, compiled code) */
    M[RV_ERRV] = R0_NONE; M[RV_ERRT] = R0_NONE; M[RV_ERRI] = R0_NONE; M[RV_ERRA] = R0_NONE;
#ifdef R0_S1_PROFILE
    for (cell c = PF_BASE; c <= PF_HASH_FALLBACK_SLOTS; c++)
        if (c != PF_TRACE) M[c] = 0;
#endif

    s1_reset();
    if (g_seed_datatypes) seed_datatype_heap();
    M[GC_FREE_HINT] = M[REG_HP];        /* GLON-MEM-2: a re-based frontier */
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
/* SITE-1 (GLON-VIEW-CORE-V1): site ids start at 1 (0 is "no enclosing
 * func"); every committed parse keeps the ids it took, a failed one gives them
 * back.  The capacity is the profile's (640 in the fixed 64K slot). */
void r0_s1_site_usage(cell *count, cell *cap) {
    if (count) *count = (cell)(next_site - 1);
    if (cap) *cap = (cell)SITE_PARENT_CAP;
}

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
