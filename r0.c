/* r0.c - a very small REBOL-like language (R0) above the frozen S1 substrate.
 *
 * R0's values are tagged cells living in S1's flat memory M; its heap objects
 * (blocks, contexts, closures) are cell regions in M. The evaluator is host C
 * code that walks those objects, using a host-side data stack and control
 * stack. Non-local control transfer is realized as frame-by-frame unwind to a
 * recorded SP baseline (observably equivalent to the architecture's atomic
 * UNWIND). The RAW trapdoor runs genuine S1 machine code via s1_run.
 *
 * S1 is FROZEN: nothing here modifies s1.c/s1.h/tests.c/adversarial.c/claims.c.
 */
#include "r0.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ================================================================== state */

static int hp;                 /* R0 heap bump pointer (into M) */
static cell ds[R0_DS_SIZE];    /* R0 data stack, grows up */
static int sp;
static int sp_max;
static r0_frame cstack[R0_CS_SIZE];
static int csp;
static int csp_max;
static const char *syms[R0_MAX_SYMS];
static int nsyms;
static int next_site;

typedef struct { cell block; int off; int site; } rsite_t;
static rsite_t f_sites[R0_MAX_RSITES]; static int nf_sites;  /* func sites */
static rsite_t r_sites[R0_MAX_RSITES]; static int nr_sites;  /* return sites */

static cell global_ctx;
static int RETURN_SYM, FUNC_SYM;

static r0_transfer gT;         /* pending transfer payload */
static int g_lastN;            /* arity of last r0_eval result */

#define GEN_SP_C  32000
#define GEN_RP_C  32001
#define GEN_IP_C  32002
#define GEN_DS_TOP 31500L
#define GEN_RS_TOP 31000L
static int gen_frag_built;
static cell gen_frag_start;

/* ============================================================ diagnostics */

static int r0_error(const char *msg) {
    fprintf(stderr, "r0 error: %s\n", msg);
    return -1;
}

/* ================================================================ stacks */

static void push(cell v) { ds[sp++] = v; if (sp > sp_max) sp_max = sp; }
static cell pop(void)    { return ds[--sp]; }

/* pop the arity on top, then remove the N values, copying them (in order) to
 * tmp[0..N-1]. Leaves the stack without the result set. */
static int take_result_set(cell *tmp) {
    int N = (int)pop();
    for (int i = 0; i < N; i++) tmp[i] = ds[sp - N + i];
    sp -= N;
    return N;
}
static void put_result_set(const cell *tmp, int N) {
    for (int i = 0; i < N; i++) push(tmp[i]);
    push((cell)N);
}
/* reduce the top result set to a single value (first, or NONE if empty) */
static void reduce_top(void) {
    int N = (int)pop();
    if (N == 0) push(R0_NONE);
    else for (int i = 1; i < N; i++) pop();
}
static void discard_result_set(void) {
    int N = (int)pop();
    sp -= N;
}

/* ============================================================== allocator */

static cell r0_alloc(int n) {
    int a = (hp + 15) & ~15;
    if (a + n > R0_HEAP_LIMIT) { fprintf(stderr, "r0: heap exhausted\n"); return -1; }
    hp = a + n;
    return (cell)a;
}

/* ================================================================ interner */

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

cell r0_intern(const char *name) {
    for (int i = 0; i < nsyms; i++)
        if (strcmp(syms[i], name) == 0) return mk_word(i);
    if (nsyms >= R0_MAX_SYMS) { fprintf(stderr, "r0: symbol table full\n"); return R0_NONE; }
    syms[nsyms] = dup_str(name);
    return mk_word(nsyms++);
}

/* ============================================================ heap objects */

static cell block_len(cell b)  { return M[r0_untag(b)]; }
static cell block_get(cell b, int i) { return M[r0_untag(b) + 1 + i]; }
static void block_set(cell b, int i, cell v) { M[r0_untag(b) + 1 + i] = v; }
static void block_set_len(cell b, cell n) { M[r0_untag(b)] = n; }

static cell make_block(int cap) {
    cell p = r0_alloc(1 + cap);
    M[p] = 0;
    return mk_block(p);
}
static cell make_context(cell parent, int cap) {
    cell p = r0_alloc(CTX_DATA + 2 * cap);
    M[p + CTX_PARENT] = parent;
    M[p + CTX_COUNT] = 0;
    M[p + CTX_CAP] = (cell)cap;
    return mk_context(p);
}
static cell make_closure(cell spec, cell body, cell ctx, int site) {
    cell p = r0_alloc(CLOSURE_SIZE);
    M[p + CLOSURE_SPEC] = spec;
    M[p + CLOSURE_BODY] = body;
    M[p + CLOSURE_CTX] = ctx;
    M[p + CLOSURE_SITE] = (cell)site;
    return mk_closure(p);
}

/* ================================================================ binding */

static int lookup(cell ctx, cell word, cell *out) {
    while (r0_tag(ctx) == TAG_CONTEXT) {
        cell p = r0_untag(ctx);
        cell n = M[p + CTX_COUNT];
        for (cell i = 0; i < n; i++) {
            if (M[p + CTX_DATA + 2 * i] == word) { *out = M[p + CTX_DATA + 2 * i + 1]; return 1; }
        }
        ctx = M[p + CTX_PARENT];
    }
    return 0;
}

/* nearest-binding update: update the nearest existing binding; else add to ctx */
static void set_binding(cell ctx, cell word, cell val) {
    cell cur = ctx;
    while (r0_tag(cur) == TAG_CONTEXT) {
        cell p = r0_untag(cur);
        cell n = M[p + CTX_COUNT];
        for (cell i = 0; i < n; i++) {
            if (M[p + CTX_DATA + 2 * i] == word) { M[p + CTX_DATA + 2 * i + 1] = val; return; }
        }
        cur = M[p + CTX_PARENT];
    }
    cell p = r0_untag(ctx);
    cell n = M[p + CTX_COUNT];
    if (n >= M[p + CTX_CAP]) { fprintf(stderr, "r0: context overflow\n"); return; }
    M[p + CTX_DATA + 2 * n] = word;
    M[p + CTX_DATA + 2 * n + 1] = val;
    M[p + CTX_COUNT] = n + 1;
}

/* ================================================================== sites */

static int lookup_site(const rsite_t *tab, int ntab, cell block, int off) {
    for (int i = 0; i < ntab; i++)
        if (tab[i].block == block && tab[i].off == off) return tab[i].site;
    return -1;
}
static void record_site(rsite_t *tab, int *ntab, cell block, int off, int site) {
    if (*ntab >= R0_MAX_RSITES) { fprintf(stderr, "r0: site table full\n"); return; }
    tab[*ntab].block = block; tab[*ntab].off = off; tab[*ntab].site = site;
    (*ntab)++;
}

/* ================================================================= parser */

typedef struct {
    const char *s;
    int pos;
    int site_stack[R0_CS_SIZE];
    int site_depth;
    int err;
} parser_t;

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
    if (buf[len - 1] == ':') { buf[len - 1] = 0; return mk_set((cell)word_id(r0_intern(buf))); }
    if (buf[0] == ':')       { return mk_get((cell)word_id(r0_intern(buf + 1))); }
    if (buf[0] == '\'')      { return mk_lit((cell)word_id(r0_intern(buf + 1))); }
    return r0_intern(buf);
}

static cell parse_block(parser_t *P) {
    P->pos++; /* '[' */
    cell elems[R0_MAX_BLOCK];
    int f_off[R0_MAX_BLOCK], f_site[R0_MAX_BLOCK]; int nf = 0;
    int r_off[R0_MAX_BLOCK], r_site[R0_MAX_BLOCK]; int nr = 0;
    int n = 0;
    for (;;) {
        skip_ws(P);
        char c = P->s[P->pos];
        if (c == ']') { P->pos++; break; }
        if (c == '\0' || n >= R0_MAX_BLOCK) { P->err = 1; break; }

        int off = n;

        /* `func` keyword: assign a site id; the following body is parsed under it */
        if (c != '[' && c != '-' && !(c >= '0' && c <= '9')) {
            int start = P->pos;
            while (P->s[P->pos] && !isspace((unsigned char)P->s[P->pos])
                   && P->s[P->pos] != '[' && P->s[P->pos] != ']') P->pos++;
            int len = P->pos - start;
            if (len == 4 && strncmp(P->s + start, "func", 4) == 0) {
                f_off[nf] = off; f_site[nf] = next_site++; nf++;
                elems[n++] = r0_intern("func");
                cell spec = parse_form(P);
                if (P->site_depth < R0_CS_SIZE) P->site_stack[P->site_depth++] = f_site[nf - 1];
                cell body = parse_form(P);
                P->site_depth--;
                elems[n++] = spec;
                elems[n++] = body;
                continue;
            }
            P->pos = start; /* not func; rewind */
        }

        cell v = parse_form(P);
        if (r0_tag(v) == TAG_WORD && word_id(v) == RETURN_SYM) {
            if (P->site_depth == 0) P->err = 2; /* return outside function */
            else { r_off[nr] = off; r_site[nr] = P->site_stack[P->site_depth - 1]; nr++; }
        }
        elems[n++] = v;
    }
    cell p = r0_alloc(1 + n);
    M[p] = (cell)n;
    for (int i = 0; i < n; i++) M[p + 1 + i] = elems[i];
    cell b = mk_block(p);
    for (int i = 0; i < nf; i++) record_site(f_sites, &nf_sites, p, f_off[i], f_site[i]);
    for (int i = 0; i < nr; i++) record_site(r_sites, &nr_sites, p, r_off[i], r_site[i]);
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

cell r0_parse(const char *src, int *errline) {
    parser_t P;
    P.s = src; P.pos = 0; P.site_depth = 0; P.err = 0;
    *errline = 0;
    skip_ws(&P);
    if (P.s[P.pos] == '[') {
        cell b = parse_block(&P);
        if (P.err) *errline = P.err;
        return b;
    }
    /* bare sequence of top-level forms */
    cell b = make_block(R0_MAX_BLOCK);
    int n = 0;
    while (P.s[P.pos] && !P.err) {
        skip_ws(&P);
        if (!P.s[P.pos]) break;
        block_set(b, n++, parse_form(&P));
        if (n >= R0_MAX_BLOCK) { P.err = 1; break; }
    }
    block_set_len(b, (cell)n);
    if (P.err) *errline = P.err;
    return b;
}

/* ============================================================= evaluator */

static int eval_block(cell blk, cell ctx);
static int eval_subexpr(cell blk, int *off, cell ctx);
static int apply(cell f, cell blk, int *off, cell ctx);
static void mold(cell v);

static int callable_arity(cell f) {
    if (r0_tag(f) == TAG_CLOSURE)
        return (int)block_len(M[r0_untag(f) + CLOSURE_SPEC]);
    if (r0_tag(f) == TAG_NATIVE) {
        static const int arity[N_NATIVES] = {
            [N_ADD]=2,[N_SUB]=2,[N_MUL]=2,[N_DIV]=2,
            [N_EQ]=2,[N_NE]=2,[N_LT]=2,[N_GT]=2,[N_LE]=2,[N_GE]=2,
            [N_PRINT]=1,[N_IF]=2,[N_LOOP]=2,[N_DO]=1,[N_VALUES]=1,
            [N_BREAK]=0,[N_THROW]=1,[N_CATCH]=1,[N_LEN]=1,[N_PICK]=2,
            [N_MAKE_GEN]=0,[N_NEXT]=1,
        };
        return arity[native_id(f)];
    }
    return 0; /* RAW takes no R0 arguments */
}

static int is_truthy(cell v) {
    if (r0_tag(v) == TAG_NONE) return 0;
    if (r0_tag(v) == TAG_INT && int_val(v) == 0) return 0;
    return 1;
}

static int eval_return(cell blk, int tok_off, int *off, cell ctx) {
    int site = lookup_site(r_sites, nr_sites, r0_untag(blk), tok_off);
    if (site < 0) return r0_error("return outside function");
    int st = eval_subexpr(blk, off, ctx);
    if (st != ST_NORMAL) return st;
    gT.nvals = take_result_set(gT.vals);
    gT.target_site = site;
    return ST_RETURN;
}

static int eval_func(cell blk, int tok_off, int *off, cell ctx) {
    int site = lookup_site(f_sites, nf_sites, r0_untag(blk), tok_off);
    if (site < 0) site = next_site++;
    int st = eval_subexpr(blk, off, ctx);
    if (st != ST_NORMAL) return st;
    reduce_top(); cell spec = pop();
    st = eval_subexpr(blk, off, ctx);
    if (st != ST_NORMAL) return st;
    reduce_top(); cell body = pop();
    push(make_closure(spec, body, ctx, site));
    push(1);
    return ST_NORMAL;
}

static int eval_subexpr(cell blk, int *off, cell ctx) {
    cell baddr = r0_untag(blk);
    if (*off >= (int)block_len(blk)) return r0_error("unexpected end of block");
    cell tok = M[baddr + 1 + (*off)];
    int tok_off = (*off)++;
    switch (r0_tag(tok)) {
    case TAG_INT: case TAG_NONE: case TAG_BLOCK: case TAG_CONTEXT:
    case TAG_CLOSURE: case TAG_NATIVE: case TAG_RAW:
        push(tok); push(1); return ST_NORMAL;
    case TAG_WORD: {
        if (word_id(tok) == RETURN_SYM) return eval_return(blk, tok_off, off, ctx);
        if (word_id(tok) == FUNC_SYM)  return eval_func(blk, tok_off, off, ctx);
        cell v;
        if (!lookup(ctx, tok, &v)) { fprintf(stderr, "r0 error: unbound word\n"); return -1; }
        if (r0_tag(v) == TAG_CLOSURE || r0_tag(v) == TAG_NATIVE || r0_tag(v) == TAG_RAW)
            return apply(v, blk, off, ctx);
        push(v); push(1); return ST_NORMAL;
    }
    case TAG_GET: {
        cell v;
        if (!lookup(ctx, mk_word(word_id(tok)), &v)) { fprintf(stderr, "r0 error: unbound word\n"); return -1; }
        push(v); push(1); return ST_NORMAL;
    }
    case TAG_LIT:
        push(mk_word(word_id(tok))); push(1); return ST_NORMAL;
    case TAG_SET: {
        int st = eval_subexpr(blk, off, ctx);
        if (st != ST_NORMAL) return st;
        reduce_top();
        cell v = pop();
        set_binding(ctx, mk_word(word_id(tok)), v);
        push(v); push(1); return ST_NORMAL;
    }
    }
    return r0_error("bad token");
}

/* apply a closure/native/raw; returns a status, leaving result set on the data
 * stack on ST_NORMAL. caller_sp is the data-stack depth before argument
 * evaluation (the caller's baseline). */
static int apply(cell f, cell blk, int *off, cell ctx) {
    int ftag = r0_tag(f);
    int arity = callable_arity(f);
    int caller_sp = sp;
    cell args[R0_MAX_ARITY];
    for (int i = 0; i < arity; i++) {
        int st = eval_subexpr(blk, off, ctx);
        if (st != ST_NORMAL) { sp = caller_sp; return st; }
        reduce_top();
        args[i] = pop();
    }
    sp = caller_sp;
    if (ftag == TAG_CLOSURE) {
        cell p = r0_untag(f);
        cell spec = M[p + CLOSURE_SPEC];
        cell body = M[p + CLOSURE_BODY];
        cell cap = M[p + CLOSURE_CTX];
        int site = (int)M[p + CLOSURE_SITE];
        cell child = make_context(cap, R0_MAX_ARITY);
        for (int i = 0; i < arity; i++) set_binding(child, block_get(spec, i), args[i]);
        cstack[csp].kind = 1; cstack[csp].id = site; cstack[csp].saved_sp = caller_sp;
        csp++; if (csp > csp_max) csp_max = csp;
        int st = eval_block(body, child);
        if (st == ST_NORMAL) {
            cell tmp[R0_MAX_ARITY];
            int N = take_result_set(tmp);
            csp--; sp = caller_sp; put_result_set(tmp, N);
            return ST_NORMAL;
        } else if (st == ST_RETURN && gT.target_site == site) {
            csp--; sp = caller_sp; put_result_set(gT.vals, gT.nvals);
            return ST_NORMAL;
        } else {
            csp--; sp = caller_sp; return st;
        }
    }
    if (ftag == TAG_NATIVE) {
        int nid = (int)native_id(f);
        switch (nid) {
        case N_ADD: case N_SUB: case N_MUL: case N_DIV: {
            cell a = int_val(args[0]), b = int_val(args[1]), r = 0;
            if (nid == N_ADD) r = a + b;
            else if (nid == N_SUB) r = a - b;
            else if (nid == N_MUL) r = a * b;
            else r = a / b;
            push(mk_int(r)); push(1); return ST_NORMAL;
        }
        case N_EQ: case N_NE: case N_LT: case N_GT: case N_LE: case N_GE: {
            cell a = int_val(args[0]), b = int_val(args[1]); int c = 0;
            if (nid == N_EQ) c = (a == b);
            else if (nid == N_NE) c = (a != b);
            else if (nid == N_LT) c = (a < b);
            else if (nid == N_GT) c = (a > b);
            else if (nid == N_LE) c = (a <= b);
            else c = (a >= b);
            push(mk_int(c)); push(1); return ST_NORMAL;
        }
        case N_PRINT: {
            /* raw host op: print the value; trampoline constructs [0] below */
            mold(args[0]); printf("\n"); fflush(stdout);
            push(0); return ST_NORMAL;
        }
        case N_IF:
            if (is_truthy(args[0])) return eval_block(args[1], ctx);
            push(R0_NONE); push(1); return ST_NORMAL;
        case N_LOOP: {
            cell n = int_val(args[0]);
            cell body = args[1];
            int saved_sp = sp;
            cstack[csp].kind = 0; cstack[csp].id = 0; cstack[csp].saved_sp = saved_sp;
            csp++; if (csp > csp_max) csp_max = csp;
            for (cell i = 0; i < n; i++) {
                int st = eval_block(body, ctx);
                if (st == ST_NORMAL) { discard_result_set(); }
                else if (st == ST_BREAK) { csp--; sp = saved_sp; push(R0_NONE); push(1); return ST_NORMAL; }
                else { csp--; sp = saved_sp; return st; }
            }
            csp--; sp = saved_sp; push(R0_NONE); push(1);
            return ST_NORMAL;
        }
        case N_DO:
            return eval_block(args[0], ctx);
        case N_VALUES: {
            cell b = args[0];
            int n = (int)block_len(b);
            int off = 0;
            cell collected[R0_MAX_ARITY];
            int cnt = 0;
            while (off < n) {
                int st = eval_subexpr(b, &off, ctx);
                if (st != ST_NORMAL) return st;
                int N = (int)pop();
                if (N != 1) return r0_error("values: each element must yield one value");
                collected[cnt++] = pop();
            }
            for (int i = 0; i < cnt; i++) push(collected[i]);
            push((cell)cnt);
            return ST_NORMAL;
        }
        case N_BREAK:
            return ST_BREAK;
        case N_THROW:
            gT.vals[0] = args[0]; gT.nvals = 1;
            return ST_THROW;
        case N_CATCH: {
            cell body = args[0];
            int saved_sp = sp;
            cstack[csp].kind = 2; cstack[csp].id = 0; cstack[csp].saved_sp = saved_sp;
            csp++; if (csp > csp_max) csp_max = csp;
            int st = eval_block(body, ctx);
            if (st == ST_THROW) { csp--; sp = saved_sp; put_result_set(gT.vals, gT.nvals); return ST_NORMAL; }
            if (st == ST_NORMAL) {
                cell tmp[R0_MAX_ARITY]; int N = take_result_set(tmp);
                csp--; sp = saved_sp; put_result_set(tmp, N);
                return ST_NORMAL;
            }
            csp--; sp = saved_sp; return st;
        }
        case N_LEN:
            push(mk_int(block_len(args[0]))); push(1); return ST_NORMAL;
        case N_PICK: {
            cell idx = int_val(args[1]);
            push(block_get(args[0], (int)idx - 1)); push(1); return ST_NORMAL;
        }
        case N_MAKE_GEN: {
            if (!gen_frag_built) { fprintf(stderr, "r0: generator fragment not built\n"); return -1; }
            M[GEN_SP_C] = GEN_DS_TOP;
            M[GEN_RP_C] = GEN_RS_TOP;
            M[GEN_IP_C] = gen_frag_start;
            cell b = make_block(1);
            block_set(b, 0, mk_int(0)); block_set_len(b, 1);
            push(b); push(1); return ST_NORMAL;
        }
        case N_NEXT: {
            cell saved_sp = M[GEN_SP_C];
            /* guard: generator data-stack pointer must stay in its region */
            if (saved_sp > GEN_DS_TOP || saved_sp < GEN_DS_TOP - 512)
                return r0_error("generator SP out of range");
            s1_set_mem(REG_SP, saved_sp);
            s1_set_mem(REG_RP, M[GEN_RP_C]);
            s1_run(M[GEN_IP_C]);
            cell yv = M[M[GEN_SP_C]];   /* raw yielded value on the generator's S1 stack */
            if (yv == -1) { push(R0_NONE); push(1); return ST_NORMAL; } /* done sentinel */
            push(mk_int(yv)); push(1); return ST_NORMAL;
        }
        }
        return r0_error("unknown native");
    }
    if (ftag == TAG_RAW) {
        /* raw-apply: run the fragment on the S1 machine; no R0 args, no result
         * (the generator uses N_NEXT for stateful resumption instead). */
        s1_set_mem(REG_SP, GEN_DS_TOP);
        s1_set_mem(REG_RP, GEN_RS_TOP);
        s1_run(r0_untag(f));
        push(0); return ST_NORMAL;
    }
    return r0_error("not callable");
}

static int eval_block(cell blk, cell ctx) {
    cell baddr = r0_untag(blk);
    int n = (int)M[baddr];
    int off = 0;
    int produced = 0;
    while (off < n) {
        int st = eval_subexpr(blk, &off, ctx);
        if (st != ST_NORMAL) return st;
        produced = 1;
        if (off < n) discard_result_set();
    }
    if (!produced) { push(R0_NONE); push(1); }
    return ST_NORMAL;
}

/* ================================================================== mold */

static void mold(cell v) {
    switch (r0_tag(v)) {
    case TAG_INT:    printf("%ld", (long)int_val(v)); break;
    case TAG_NONE:   printf("none"); break;
    case TAG_WORD:   printf("%s", syms[word_id(v)]); break;
    case TAG_SET:    printf("%s:", syms[word_id(v)]); break;
    case TAG_GET:    printf(":%s", syms[word_id(v)]); break;
    case TAG_LIT:    printf("'%s", syms[word_id(v)]); break;
    case TAG_BLOCK: {
        printf("[");
        cell n = block_len(v);
        for (cell i = 0; i < n; i++) { if (i) printf(" "); mold(block_get(v, (int)i)); }
        printf("]");
        break;
    }
    case TAG_CONTEXT: printf("<context>"); break;
    case TAG_CLOSURE: printf("<func>"); break;
    case TAG_NATIVE:  printf("<native>"); break;
    case TAG_RAW:     printf("<raw>"); break;
    }
}

/* ================================================================ init */

static void build_gen_fragment(void) {
    /* emit_gen_yield: save {SP,RP,IP} to GEN_* cells, then HALT (continuation
     * patched to just after the HALT). */
    /* (inline via the S1 assembler) */
    #define EMIT_YIELD() do { \
        asm_lit(REG_SP); asm_fetch(); asm_lit(GEN_SP_C); asm_store(); \
        asm_lit(REG_RP); asm_fetch(); asm_lit(GEN_RP_C); asm_store(); \
        { cell _p = asm_lit_fwd(); asm_lit(GEN_IP_C); asm_store(); \
          asm_halt(); asm_patch_here(_p); } \
    } while (0)

    asm_reset();
    gen_frag_start = asm_here();
    asm_lit(1);  EMIT_YIELD();
    asm_lit(2);  EMIT_YIELD();
    asm_lit(3);  EMIT_YIELD();
    asm_lit(-1); EMIT_YIELD();  /* -1 = done sentinel (distinct from yields) */
    asm_halt();
    gen_frag_built = 1;
    #undef EMIT_YIELD
}

cell r0_init(void) {
    hp = R0_HEAP_BASE;
    sp = 0;
    csp = 0;
    nsyms = 0; next_site = 1;
    nf_sites = 0; nr_sites = 0;
    g_lastN = 0;
    gT.status = ST_NORMAL;

    RETURN_SYM = (int)word_id(r0_intern("return"));
    FUNC_SYM = (int)word_id(r0_intern("func"));

    global_ctx = make_context(R0_NONE, R0_MAX_BINDINGS);

    #define BIND(nm, id) set_binding(global_ctx, r0_intern(nm), mk_native(id))
    BIND("+", N_ADD); BIND("-", N_SUB); BIND("*", N_MUL); BIND("/", N_DIV);
    BIND("=", N_EQ); BIND("!=", N_NE); BIND("<", N_LT); BIND(">", N_GT);
    BIND("<=", N_LE); BIND(">=", N_GE);
    BIND("print", N_PRINT); BIND("if", N_IF); BIND("loop", N_LOOP);
    BIND("do", N_DO); BIND("values", N_VALUES); BIND("break", N_BREAK);
    BIND("throw", N_THROW); BIND("catch", N_CATCH);
    BIND("len", N_LEN); BIND("pick", N_PICK);
    BIND("make-gen", N_MAKE_GEN); BIND("next", N_NEXT);
    #undef BIND

    build_gen_fragment();
    return global_ctx;
}

/* ============================================================ public api */

int r0_eval(cell block, cell ctx) {
    sp = 0;
    csp = 0;
    gT.status = ST_NORMAL;
    int st = eval_block(block, ctx);
    if (st < 0) return -1;
    if (st == ST_NORMAL) {
        g_lastN = (int)ds[sp - 1];
        return g_lastN;
    }
    const char *what = st == ST_BREAK ? "break outside loop"
                     : st == ST_RETURN ? "return without function"
                     : "throw without catch";
    return r0_error(what);
}

cell r0_result(int i) { return ds[sp - g_lastN - 1 + i]; }

int r0_ds_depth(void) { return sp; }
int r0_ds_max(void) { return sp_max; }
int r0_cs_depth(void) { return csp; }
int r0_cs_max(void) { return csp_max; }
cell r0_global_context(void) { return global_ctx; }
int r0_symbol_count(void) { return nsyms; }
long r0_heap_used(void) { return (long)(hp - R0_HEAP_BASE); }
