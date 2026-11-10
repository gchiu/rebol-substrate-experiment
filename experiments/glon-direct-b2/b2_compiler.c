/* B1: a small, direct Glon-to-S1 compiler subset.
 *
 * Supported:
 *   name: func [p ...] [ body ]      top-level definitions only
 *   integer literals, including negative literals
 *   parameters
 *   calls to defined functions (including recursion and forward references)
 *   + - * and < <= = > >=
 *   either condition [then] [else]
 *
 * Not supported: closures, dynamic binding, escape laws, blocks as values,
 * set-words/locals, strings, nested funcs, if without else, / and other
 * natives, more than 8 parameters or arguments, more than 32 functions.
 *
 * Calling convention: the caller pushes arguments left to right. The callee
 * saves them on the return stack, evaluates its body, discards the saved
 * arguments, and EXITs. A call leaves exactly one result on the data stack.
 * No heap, loader, GC or runtime-state cells are used by the compiled code.
 */
#include "b2_compiler.h"
#include "b2_machine.h"
#include "r0_s1.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 4096
#define MAX_FUNCS 32
#define MAX_PARAMS 8
#define MAX_ARGS 8
#define MAX_SEQ 128
#define MAX_NODES 2048
#define MAX_NAME 48

enum { TK_WORD, TK_INT, TK_LBRACK, TK_RBRACK, TK_EOF };

typedef struct {
    int kind;
    long ival;
    char text[MAX_NAME];
} Tok;

typedef enum { EX_INT, EX_PARAM, EX_BIN, EX_CALL, EX_EITHER, EX_SEQ } ExprKind;
typedef enum { B_ADD, B_SUB, B_MUL, B_LT, B_LE, B_EQ, B_GT, B_GE } BinOp;

typedef struct Expr {
    ExprKind kind;
    long ival;
    int param;
    BinOp op;
    char name[MAX_NAME];
    struct Expr *a, *b, *c;
    struct Expr *list[MAX_SEQ];
    int nlist;
} Expr;

typedef struct {
    char name[MAX_NAME];
    char params[MAX_PARAMS][MAX_NAME];
    int nparams;
    int body_open;
    Expr *body;
    cell entry;
} Func;

typedef struct {
    Tok tok[MAX_TOKENS];
    int ntok;
    int pos;

    Func funcs[MAX_FUNCS];
    int nfuncs;
    int top_start;

    Expr pool[MAX_NODES];
    int npool;

    char params[MAX_PARAMS][MAX_NAME];
    int nparams;
    unsigned flags;
    const char *error;
} Parser;

static void seterr(Parser *p, const char *fmt, ...) {
    static char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (!p->error) p->error = buf;
}

static int tokenize(Parser *p, const char *s) {
    p->ntok = 0;
    while (*s) {
        if (isspace((unsigned char)*s)) { s++; continue; }
        if (*s == ';') { while (*s && *s != '\n') s++; continue; }
        if (*s == '[' || *s == ']') {
            if (p->ntok >= MAX_TOKENS) { seterr(p, "token limit"); return 0; }
            Tok *t = &p->tok[p->ntok++];
            t->kind = (*s == '[') ? TK_LBRACK : TK_RBRACK;
            t->ival = 0; t->text[0] = 0;
            s++;
            continue;
        }
        if (isdigit((unsigned char)*s) ||
            (*s == '-' && isdigit((unsigned char)s[1]))) {
            char *end = NULL;
            long v = strtol(s, &end, 10);
            if (end == s) { seterr(p, "bad integer"); return 0; }
            if (p->ntok >= MAX_TOKENS) { seterr(p, "token limit"); return 0; }
            Tok *t = &p->tok[p->ntok++];
            t->kind = TK_INT; t->ival = v; t->text[0] = 0;
            s = end;
            continue;
        }
        size_t n = 0;
        while (*s && !isspace((unsigned char)*s) && *s != '[' && *s != ']' && *s != ';') {
            if (n + 1 >= sizeof p->tok[0].text) { seterr(p, "word too long"); return 0; }
            n++;
            s++;
        }
        if (p->ntok >= MAX_TOKENS) { seterr(p, "token limit"); return 0; }
        Tok *t = &p->tok[p->ntok++];
        t->kind = TK_WORD; t->ival = 0;
        const char *a = s - n;
        memcpy(t->text, a, n);
        t->text[n] = 0;
    }
    if (p->ntok >= MAX_TOKENS) { seterr(p, "token limit"); return 0; }
    Tok *t = &p->tok[p->ntok++];
    t->kind = TK_EOF; t->ival = 0; t->text[0] = 0;
    return 1;
}

static int find_func(const Parser *p, const char *name) {
    for (int i = 0; i < p->nfuncs; i++)
        if (!strcmp(p->funcs[i].name, name)) return i;
    return -1;
}

/* Collect function names and arities before parsing bodies, so a function may
 * call one defined later. */
static int prescan(Parser *p) {
    int i = 0;
    while (i < p->ntok) {
        Tok *t = &p->tok[i];
        if (t->kind != TK_WORD || t->text[0] == 0 ||
            t->text[strlen(t->text) - 1] != ':')
            break;
        if (i + 2 >= p->ntok || p->tok[i + 1].kind != TK_WORD ||
            strcmp(p->tok[i + 1].text, "func") != 0 ||
            p->tok[i + 2].kind != TK_LBRACK) {
            seterr(p, "expected 'name: func [params] [body]'");
            return 0;
        }
        if (p->nfuncs >= MAX_FUNCS) { seterr(p, "too many functions"); return 0; }
        Func *f = &p->funcs[p->nfuncs];
        size_t nl = strlen(t->text) - 1;
        if (nl == 0 || nl >= sizeof f->name) { seterr(p, "bad function name"); return 0; }
        memcpy(f->name, t->text, nl);
        f->name[nl] = 0;
        if (find_func(p, f->name) >= 0) { seterr(p, "duplicate function '%s'", f->name); return 0; }

        int j = i + 3;
        f->nparams = 0;
        while (j < p->ntok && p->tok[j].kind != TK_RBRACK) {
            if (p->tok[j].kind != TK_WORD) { seterr(p, "parameter must be a word"); return 0; }
            if (f->nparams >= MAX_PARAMS) { seterr(p, "too many parameters in '%s'", f->name); return 0; }
            for (int k = 0; k < f->nparams; k++)
                if (!strcmp(f->params[k], p->tok[j].text)) {
                    seterr(p, "duplicate parameter '%s'", p->tok[j].text);
                    return 0;
                }
            snprintf(f->params[f->nparams], sizeof f->params[0], "%s", p->tok[j].text);
            f->nparams++;
            j++;
        }
        if (j >= p->ntok || p->tok[j].kind != TK_RBRACK) {
            seterr(p, "unterminated parameter list in '%s'", f->name);
            return 0;
        }
        j++;
        if (j >= p->ntok || p->tok[j].kind != TK_LBRACK) {
            seterr(p, "expected body block after '%s'", f->name);
            return 0;
        }
        f->body_open = j;
        int depth = 0;
        while (j < p->ntok) {
            if (p->tok[j].kind == TK_LBRACK) depth++;
            else if (p->tok[j].kind == TK_RBRACK) {
                depth--;
                if (depth == 0) { j++; break; }
            }
            j++;
        }
        if (depth != 0) { seterr(p, "unterminated body block in '%s'", f->name); return 0; }
        p->nfuncs++;
        i = j;
    }
    p->top_start = i;
    if (i >= p->ntok || p->tok[i].kind == TK_EOF) {
        seterr(p, "no top-level expression");
        return 0;
    }
    return 1;
}

static Expr *new_expr(Parser *p, ExprKind k) {
    if (p->npool >= MAX_NODES) { seterr(p, "expression too large"); return NULL; }
    Expr *e = &p->pool[p->npool++];
    memset(e, 0, sizeof *e);
    e->kind = k;
    return e;
}

static int is_param(const Parser *p, const char *name) {
    for (int i = 0; i < p->nparams; i++)
        if (!strcmp(p->params[i], name)) return i;
    return -1;
}

static int binop_for(const char *word, BinOp *op) {
    if (!strcmp(word, "+")) { *op = B_ADD; return 1; }
    if (!strcmp(word, "-")) { *op = B_SUB; return 1; }
    if (!strcmp(word, "*")) { *op = B_MUL; return 1; }
    if (!strcmp(word, "<")) { *op = B_LT; return 1; }
    if (!strcmp(word, "<=")) { *op = B_LE; return 1; }
    if (!strcmp(word, "=")) { *op = B_EQ; return 1; }
    if (!strcmp(word, ">")) { *op = B_GT; return 1; }
    if (!strcmp(word, ">=")) { *op = B_GE; return 1; }
    return 0;
}

static Expr *parse_seq(Parser *p, int in_block);

static Expr *parse_expr(Parser *p) {
    if (p->pos >= p->ntok) { seterr(p, "unexpected end of input"); return NULL; }
    Tok *t = &p->tok[p->pos];
    if (t->kind == TK_INT) {
        Expr *e = new_expr(p, EX_INT);
        if (!e) return NULL;
        e->ival = t->ival; p->pos++;
        return e;
    }
    if (t->kind != TK_WORD) {
        seterr(p, "expected an expression, found %s",
               t->kind == TK_LBRACK ? "'['" : t->kind == TK_RBRACK ? "']'" : "end of input");
        return NULL;
    }

    int pi = is_param(p, t->text);
    if (pi >= 0) {
        Expr *e = new_expr(p, EX_PARAM);
        if (!e) return NULL;
        e->param = pi; p->pos++;
        return e;
    }

    if (!strcmp(t->text, "either")) {
        p->pos++;
        Expr *e = new_expr(p, EX_EITHER);
        if (!e) return NULL;
        e->a = parse_expr(p);
        if (!e->a) return NULL;
        if (p->pos >= p->ntok || p->tok[p->pos].kind != TK_LBRACK) {
            seterr(p, "either: expected '[' before the then block"); return NULL;
        }
        p->pos++;
        e->b = parse_seq(p, 1);
        if (!e->b) return NULL;
        if (p->pos >= p->ntok || p->tok[p->pos].kind != TK_RBRACK) {
            seterr(p, "either: unterminated then block"); return NULL;
        }
        p->pos++;
        if (p->pos >= p->ntok || p->tok[p->pos].kind != TK_LBRACK) {
            seterr(p, "either: expected '[' before the else block"); return NULL;
        }
        p->pos++;
        e->c = parse_seq(p, 1);
        if (!e->c) return NULL;
        if (p->pos >= p->ntok || p->tok[p->pos].kind != TK_RBRACK) {
            seterr(p, "either: unterminated else block"); return NULL;
        }
        p->pos++;
        return e;
    }

    BinOp op;
    if (binop_for(t->text, &op)) {
        p->pos++;
        Expr *e = new_expr(p, EX_BIN);
        if (!e) return NULL;
        e->op = op;
        e->a = parse_expr(p); if (!e->a) return NULL;
        e->b = parse_expr(p); if (!e->b) return NULL;
        return e;
    }

    int fi = find_func(p, t->text);
    if (fi >= 0) {
        p->pos++;
        Expr *e = new_expr(p, EX_CALL);
        if (!e) return NULL;
        snprintf(e->name, sizeof e->name, "%s", t->text);
        e->nlist = p->funcs[fi].nparams;
        for (int i = 0; i < e->nlist; i++) {
            e->list[i] = parse_expr(p);
            if (!e->list[i]) return NULL;
        }
        return e;
    }

    seterr(p, "unsupported word or unknown name '%s'", t->text);
    return NULL;
}

static Expr *parse_seq(Parser *p, int in_block) {
    Expr *e = new_expr(p, EX_SEQ);
    if (!e) return NULL;
    while (p->pos < p->ntok) {
        if (p->tok[p->pos].kind == TK_RBRACK) {
            if (in_block) break;
            seterr(p, "unexpected ']'");
            return NULL;
        }
        if (p->tok[p->pos].kind == TK_EOF) break;
        Expr *x = parse_expr(p);
        if (!x) return NULL;
        if (e->nlist >= MAX_SEQ) { seterr(p, "sequence too long"); return NULL; }
        e->list[e->nlist++] = x;
    }
    if (e->nlist == 0) { seterr(p, "empty expression sequence"); return NULL; }
    return e;
}

typedef struct { cell patch; char name[MAX_NAME]; } CallPatch;
#define MAX_PATCHES 256

static int compile_expr(Parser *p, Expr *e, CallPatch *patches, int *npatch);

static void emit_param(Parser *p, int index) {
    if (index == 0) {
        if (p->flags & B2_NATIVE_RFETCH) b2_native_fetchR();
        else asm_fetchR();
    } else {
        asm_lit(REG_RP);
        asm_fetch();
        asm_lit(index);
        asm_add();
        asm_fetch();
    }
}

static int compile_call(Parser *p, Expr *e, CallPatch *patches, int *npatch) {
    for (int i = 0; i < e->nlist; i++)
        if (!compile_expr(p, e->list[i], patches, npatch)) return 0;
    if (*npatch >= MAX_PATCHES) { seterr(p, "too many call sites"); return 0; }

    cell target;
    if (p->flags & B2_NATIVE_CALL) {
        target = b2_native_call_fwd();
    } else {
        cell cont = asm_lit_fwd();   /* continuation literal */
        asm_toR();                   /* push continuation */
        target = asm_lit_fwd();      /* callee literal, patched later */
        asm_lit(REG_IP);
        asm_store();
        s1_set_mem(cont, asm_here());
    }

    patches[*npatch].patch = target;
    snprintf(patches[*npatch].name, sizeof patches[*npatch].name, "%s", e->name);
    (*npatch)++;
    return 1;
}

static int compile_expr(Parser *p, Expr *e, CallPatch *patches, int *npatch) {
    switch (e->kind) {
    case EX_INT:
        asm_lit(mk_int(e->ival));
        return 1;
    case EX_PARAM:
        emit_param(p, e->param);
        return 1;
    case EX_BIN:
        if (e->op == B_MUL) {
            if (!compile_expr(p, e->a, patches, npatch)) return 0;
            asm_lit(16); asm_div();          /* untag a */
            if (!compile_expr(p, e->b, patches, npatch)) return 0;
            asm_lit(16); asm_div();          /* untag b */
            asm_mul();
            asm_lit(16); asm_mul();          /* tag result */
            return 1;
        }
        if (!compile_expr(p, e->a, patches, npatch)) return 0;
        if (!compile_expr(p, e->b, patches, npatch)) return 0;
        switch (e->op) {
        case B_ADD: asm_add(); break;
        case B_SUB: asm_sub(); break;
        case B_LT:  asm_lt();  break;
        case B_LE:  asm_le();  break;
        case B_EQ:  asm_eq();  break;
        case B_GT:  asm_gt();  break;
        case B_GE:  asm_ge();  break;
        default: seterr(p, "internal: bad binary op"); return 0;
        }
        if (e->op != B_ADD && e->op != B_SUB) {
            asm_lit(16); asm_mul();          /* comparison -> tagged 1/0 */
        }
        return 1;
    case EX_EITHER: {
        if (!compile_expr(p, e->a, patches, npatch)) return 0;
        cell jelse = asm_zbranch_fwd();
        Expr *then_seq = e->b, *else_seq = e->c;
        for (int i = 0; i < then_seq->nlist; i++) {
            if (!compile_expr(p, then_seq->list[i], patches, npatch)) return 0;
            if (i + 1 < then_seq->nlist) asm_drop();
        }
        cell jend = (p->flags & B2_NATIVE_BRANCH) ? b2_native_branch_fwd() : asm_branch_fwd();
        s1_set_mem(jelse, asm_here());
        for (int i = 0; i < else_seq->nlist; i++) {
            if (!compile_expr(p, else_seq->list[i], patches, npatch)) return 0;
            if (i + 1 < else_seq->nlist) asm_drop();
        }
        s1_set_mem(jend, asm_here());
        return 1;
    }
    case EX_CALL:
        return compile_call(p, e, patches, npatch);
    case EX_SEQ:
        for (int i = 0; i < e->nlist; i++) {
            if (!compile_expr(p, e->list[i], patches, npatch)) return 0;
            if (i + 1 < e->nlist) asm_drop();
        }
        return 1;
    }
    seterr(p, "internal: unknown expression kind");
    return 0;
}

int b2_compile(const char *source, unsigned flags, b2_code *out, char *err, size_t errlen) {
    Parser p;
    memset(&p, 0, sizeof p);
    p.flags = flags;
    memset(out, 0, sizeof *out);
    out->flags = flags;
    if (err && errlen) err[0] = 0;

    if (!tokenize(&p, source)) goto fail;
    if (!prescan(&p)) goto fail;

    /* Parse function bodies with their parameter scopes. */
    for (int i = 0; i < p.nfuncs; i++) {
        p.nparams = p.funcs[i].nparams;
        for (int k = 0; k < p.nparams; k++)
            snprintf(p.params[k], sizeof p.params[k], "%s", p.funcs[i].params[k]);
        p.pos = p.funcs[i].body_open + 1;
        p.funcs[i].body = parse_seq(&p, 1);
        if (!p.funcs[i].body) goto fail;
        if (p.pos >= p.ntok || p.tok[p.pos].kind != TK_RBRACK) {
            seterr(&p, "unterminated body in '%s'", p.funcs[i].name);
            goto fail;
        }
        p.pos++;
    }

    /* Top-level expression(s). */
    p.nparams = 0;
    p.pos = p.top_start;
    Expr *top = parse_seq(&p, 0);
    if (!top) goto fail;
    if (p.pos != p.ntok - 1) {              /* only TK_EOF should remain */
        seterr(&p, "unexpected tokens after the top-level expression");
        goto fail;
    }

    asm_reset();
    CallPatch patches[MAX_PATCHES];
    int npatch = 0;

    for (int i = 0; i < p.nfuncs; i++) {
        p.funcs[i].entry = asm_here();
        for (int k = 0; k < p.funcs[i].nparams; k++) {
            if (p.flags & B2_NATIVE_TO_R) b2_native_toR(); else asm_toR();
        }
        if (!compile_expr(&p, p.funcs[i].body, patches, &npatch)) goto fail;
        for (int k = 0; k < p.funcs[i].nparams; k++) {
            if (p.flags & B2_NATIVE_FROM_R) b2_native_fromR(); else asm_fromR();
            asm_drop();
        }
        if (p.flags & B2_NATIVE_RET) b2_native_ret(); else asm_exit();
    }

    out->entry = asm_here();
    if (!compile_expr(&p, top, patches, &npatch)) goto fail;
    asm_halt();

    for (int i = 0; i < npatch; i++) {
        int fi = find_func(&p, patches[i].name);
        if (fi < 0) { seterr(&p, "call to undefined function '%s'", patches[i].name); goto fail; }
        s1_set_mem(patches[i].patch, p.funcs[fi].entry);
    }

    out->code_start = 256;
    out->code_end = asm_here();
    out->code_cells = out->code_end - out->code_start;
    out->nfuncs = p.nfuncs;
    return 1;

fail:
    if (err && errlen) snprintf(err, errlen, "%s", p.error ? p.error : "compile error");
    return 0;
}
