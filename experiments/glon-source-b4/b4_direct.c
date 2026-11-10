/* B4-C: direct machine-oriented word assembler with labels and relocation. */
#include "b4_direct.h"
#include "b3_machine.h"
#include "r0_s1.h"

#include <stdio.h>
#include <string.h>

#define MAX_LABELS 256
#define MAX_PATCH 512

typedef struct { int id; cell addr; } Label;
typedef struct { int label; cell patch; int token; } Patch;

typedef struct {
    Label labels[MAX_LABELS];
    int nlabels;
    Patch patches[MAX_PATCH];
    int npatch;
    int entry_label;
    int have_entry;
    int cur_token;
    char *err;
    size_t errlen;
} Direct;

static unsigned rd32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int fail(Direct *d, const char *why) {
    if (d->err && d->errlen)
        snprintf(d->err, d->errlen, "direct token %d: %s", d->cur_token, why);
    return 0;
}

static int add_patch(Direct *d, int label, cell patch) {
    if (d->npatch >= MAX_PATCH) return 0;
    d->patches[d->npatch++] = (Patch){label, patch, d->cur_token};
    return 1;
}

int b4_direct_assemble(const unsigned char *data, size_t n,
                       b4_code *out, char *err, size_t errlen) {
    Direct d;
    memset(&d, 0, sizeof d);
    memset(out, 0, sizeof *out);
    if (err && errlen) err[0] = 0;
    d.err = err; d.errlen = errlen;

    if (!data || n < 8 || (n & 3) != 0) {
        if (err && errlen) snprintf(err, errlen, "direct: bad file size or header");
        return 0;
    }
    if (rd32(data) != B4C_MAGIC) {
        if (err && errlen) snprintf(err, errlen, "direct: bad magic");
        return 0;
    }

    asm_reset();
    size_t words = n / 4 - 1;
    for (size_t i = 0; i < words; i++) {
        unsigned w = rd32(data + 4 * (i + 1));
        int op = (int)(w >> 24);
        unsigned arg = w & 0xFFFFFFu;
        d.cur_token = (int)i + 1;
        switch (op) {
        case BC_ENTRY:
            if (d.have_entry) return fail(&d, "duplicate entry directive");
            d.have_entry = 1; d.entry_label = (int)arg;
            break;
        case BC_LABEL:
            if (arg == 0) return fail(&d, "label id 0");
            for (int k = 0; k < d.nlabels; k++)
                if (d.labels[k].id == (int)arg) return fail(&d, "duplicate label");
            if (d.nlabels >= MAX_LABELS) return fail(&d, "too many labels");
            d.labels[d.nlabels++] = (Label){(int)arg, asm_here()};
            break;
        case BC_LIT: asm_lit((cell)arg); break;
        case BC_DUP: asm_dup(); break;
        case BC_DROP: asm_drop(); break;
        case BC_FETCH: asm_fetch(); break;
        case BC_STORE: asm_store(); break;
        case BC_ZBRANCH: {
            if (arg == 0) return fail(&d, "label id 0");
            cell p = asm_zbranch_fwd();
            if (!add_patch(&d, (int)arg, p)) return fail(&d, "too many branches");
            break;
        }
        case BC_HOST:
            if (arg > HOST_PRINT) return fail(&d, "HOST id outside S1's closed set");
            asm_host((cell)arg);
            break;
        case BC_CALL: {
            if (arg == 0) return fail(&d, "label id 0");
            cell p = b3_native_call_fwd();
            if (!add_patch(&d, (int)arg, p)) return fail(&d, "too many calls");
            break;
        }
        case BC_RET: b3_native_ret(); break;
        case BC_TO_R: b3_native_toR(); break;
        case BC_FROM_R: b3_native_fromR(); break;
        case BC_R_FETCH: b3_native_fetchR(); break;
        case BC_BRANCH: {
            if (arg == 0) return fail(&d, "label id 0");
            cell p = b3_native_branch_fwd();
            if (!add_patch(&d, (int)arg, p)) return fail(&d, "too many branches");
            break;
        }
        case BC_HALT: asm_halt(); break;
        default:
            return fail(&d, "unknown operation");
        }
    }

    if (!d.have_entry) return fail(&d, "missing ENTRY directive");
    cell entry = 0;
    int found_entry = 0;
    for (int k = 0; k < d.nlabels; k++)
        if (d.labels[k].id == d.entry_label) { entry = d.labels[k].addr; found_entry = 1; break; }
    if (!found_entry) return fail(&d, "undefined entry label");

    for (int i = 0; i < d.npatch; i++) {
        cell addr = 0; int found = 0;
        for (int k = 0; k < d.nlabels; k++)
            if (d.labels[k].id == d.patches[i].label) { addr = d.labels[k].addr; found = 1; break; }
        if (!found) {
            if (err && errlen) snprintf(err, errlen,
                "direct token %d: undefined label id %d",
                d.patches[i].token, d.patches[i].label);
            return 0;
        }
        s1_set_mem(d.patches[i].patch, addr);
    }

    out->entry = entry;
    out->code_cells = asm_here() - 256;
    return 1;
}
