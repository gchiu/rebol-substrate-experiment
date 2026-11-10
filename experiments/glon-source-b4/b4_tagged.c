/* B4-B: semantically tagged token decoder and direct S1 emitter. */
#include "b4_tagged.h"
#include "b3_machine.h"
#include "r0_s1.h"

#include <stdio.h>
#include <string.h>

#define MAX_FUNCS 32
#define MAX_PATCH 512

typedef struct {
    int id;
    int arity;
    cell entry;
} Func;

typedef struct {
    int target;
    int arity;
    cell patch;
    int token;
} Patch;

enum { B_ADD, B_SUB, B_MUL, B_LT, B_LE, B_EQ, B_GT, B_GE };

typedef struct {
    const unsigned char *data;
    size_t words;       /* payload words, not including magic */
    size_t pos;
    Func funcs[MAX_FUNCS];
    int nfuncs;
    Patch patches[MAX_PATCH];
    int npatch;
    int cur_arity;
    char *err;
    size_t errlen;
} Tagged;

static unsigned rd32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int fail(Tagged *t, const char *why) {
    if (t->err && t->errlen)
        snprintf(t->err, t->errlen, "tagged token %lu: %s",
                 (unsigned long)t->pos, why);
    return 0;
}

static int role(Tagged *t, unsigned *operand) {
    if (t->pos >= t->words) return -1;
    unsigned w = rd32(t->data + 4 * (t->pos + 1));
    if (operand) *operand = w & 0xFFFFFFu;
    return (int)(w >> 24);
}

static int take(Tagged *t, unsigned *operand) {
    int r = role(t, operand);
    if (r >= 0) t->pos++;
    return r;
}

static void param_read(int index) {
    if (index == 0) {
        b3_native_fetchR();
    } else {
        asm_lit(REG_RP);
        asm_fetch();
        asm_lit(index);
        asm_add();
        asm_fetch();
    }
}

static int emit_expr(Tagged *t);

static int emit_seq(Tagged *t, int end_role) {
    int any = 0;
    for (;;) {
        int r = role(t, NULL);
        if (r < 0) return fail(t, "unexpected end inside a sequence");
        if (r == end_role) {
            if (!any) return fail(t, "empty sequence");
            return 1;
        }
        if (!emit_expr(t)) return 0;
        any = 1;
        r = role(t, NULL);
        if (r != end_role) asm_drop();
    }
}

static int emit_expr(Tagged *t) {
    unsigned op = 0;
    int r = take(t, &op);
    switch (r) {
    case BT_INT: {
        long v = (long)(op & 0x800000u ? op | 0xFF000000u : op);
        asm_lit(mk_int(v));
        return 1;
    }
    case BT_PARAM:
        if ((int)op >= t->cur_arity) return fail(t, "parameter out of range");
        param_read((int)op);
        return 1;
    case BT_CALL: {
        int id = (int)(op >> 8), arity = (int)(op & 0xFF);
        for (int i = 0; i < arity; i++)
            if (!emit_expr(t)) return 0;
        if (t->npatch >= MAX_PATCH) return fail(t, "too many call sites");
        cell patch = b3_native_call_fwd();
        t->patches[t->npatch++] = (Patch){id, arity, patch, (int)t->pos};
        return 1;
    }
    case BT_BIN: {
        if (op > B_GE) return fail(t, "unknown binary operation");
        if (op == B_MUL) {
            if (!emit_expr(t)) return 0;
            asm_lit(16); asm_div();
            if (!emit_expr(t)) return 0;
            asm_lit(16); asm_div();
            asm_mul(); asm_lit(16); asm_mul();
            return 1;
        }
        if (!emit_expr(t)) return 0;
        if (!emit_expr(t)) return 0;
        switch (op) {
        case B_ADD: asm_add(); break;
        case B_SUB: asm_sub(); break;
        case B_LT: asm_lt(); break;
        case B_LE: asm_le(); break;
        case B_EQ: asm_eq(); break;
        case B_GT: asm_gt(); break;
        case B_GE: asm_ge(); break;
        }
        if (op != B_ADD && op != B_SUB) { asm_lit(16); asm_mul(); }
        return 1;
    }
    case BT_EITHER: {
        if (!emit_expr(t)) return 0;
        cell jelse = asm_zbranch_fwd();
        if (take(t, NULL) != BT_THEN_BEGIN) return fail(t, "expected then block");
        if (!emit_seq(t, BT_THEN_END)) return 0;
        if (take(t, NULL) != BT_THEN_END) return fail(t, "unterminated then block");
        cell jend = b3_native_branch_fwd();
        s1_set_mem(jelse, asm_here());
        if (take(t, NULL) != BT_ELSE_BEGIN) return fail(t, "expected else block");
        if (!emit_seq(t, BT_ELSE_END)) return 0;
        if (take(t, NULL) != BT_ELSE_END) return fail(t, "unterminated else block");
        s1_set_mem(jend, asm_here());
        return 1;
    }
    default:
        if (r < 0) return fail(t, "unexpected end of tokens");
        return fail(t, "expected an expression");
    }
}

int b4_tagged_compile(const unsigned char *data, size_t n,
                      b4_code *out, char *err, size_t errlen) {
    Tagged t;
    memset(&t, 0, sizeof t);
    memset(out, 0, sizeof *out);
    if (err && errlen) err[0] = 0;
    t.data = data; t.err = err; t.errlen = errlen;

    if (!data || n < 8 || (n & 3) != 0) {
        if (err && errlen) snprintf(err, errlen, "tagged: bad file size or header");
        return 0;
    }
    if (rd32(data) != B4B_MAGIC) {
        if (err && errlen) snprintf(err, errlen, "tagged: bad magic");
        return 0;
    }
    t.words = n / 4 - 1;
    t.pos = 0;

    asm_reset();
    for (;;) {
        int r = role(&t, NULL);
        if (r != BT_FUNC) break;
        unsigned op;
        take(&t, &op);
        int id = (int)(op >> 8), arity = (int)(op & 0xFF);
        if (id <= 0) return fail(&t, "bad function id");
        if (arity > 8) return fail(&t, "arity above 8");
        for (int i = 0; i < t.nfuncs; i++)
            if (t.funcs[i].id == id) return fail(&t, "duplicate function id");
        if (t.nfuncs >= MAX_FUNCS) return fail(&t, "too many functions");
        Func *f = &t.funcs[t.nfuncs++];
        f->id = id; f->arity = arity; f->entry = asm_here();
        t.cur_arity = arity;
        for (int i = 0; i < arity; i++) b3_native_toR();
        if (take(&t, NULL) != BT_SEQ_BEGIN) return fail(&t, "expected function body");
        if (!emit_seq(&t, BT_SEQ_END)) return 0;
        if (take(&t, NULL) != BT_SEQ_END) return fail(&t, "unterminated function body");
        for (int i = 0; i < arity; i++) { b3_native_fromR(); asm_drop(); }
        b3_native_ret();
    }

    t.cur_arity = 0;
    if (take(&t, NULL) != BT_TOP_BEGIN) return fail(&t, "expected top-level block");
    cell top_entry = asm_here();
    if (!emit_seq(&t, BT_TOP_END)) return 0;
    if (take(&t, NULL) != BT_TOP_END) return fail(&t, "unterminated top-level block");
    if (t.pos != t.words) return fail(&t, "tokens after top-level block");
    asm_halt();

    for (int i = 0; i < t.npatch; i++) {
        Func *f = NULL;
        for (int k = 0; k < t.nfuncs; k++)
            if (t.funcs[k].id == t.patches[i].target) { f = &t.funcs[k]; break; }
        if (!f) {
            if (err && errlen) snprintf(err, errlen,
                "tagged token %d: call to undefined function id %d",
                t.patches[i].token, t.patches[i].target);
            return 0;
        }
        if (f->arity != t.patches[i].arity) {
            if (err && errlen) snprintf(err, errlen,
                "tagged token %d: call arity %d does not match function id %d arity %d",
                t.patches[i].token, t.patches[i].arity, f->id, f->arity);
            return 0;
        }
        s1_set_mem(t.patches[i].patch, f->entry);
    }

    out->entry = top_entry;
    out->code_cells = asm_here() - 256;
    return 1;
}
