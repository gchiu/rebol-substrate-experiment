/* B3 experimental machine: a copy of the B2 machine (frozen S1 semantics plus
 * six candidate native opcodes). The original seven primitives and all frozen
 * assembler expansions remain available. Counters are compiled only under
 * -DB3_COUNT; the timing build has the same semantics without them. */
#include "b11_machine.h"

#include <stdio.h>

cell M[MEM_CELLS];

/* B3 MEASUREMENT COPY: frozen S1 semantics plus the B2 opcodes. The counters
 * are optional; the production/frozen s1.c is never changed. */
#ifdef B3_COUNT
long b3_insns;
long b3_min_sp;
long b3_min_rp;
long b9_op_count[6];
long b11_total_ops;
long b11_calls;
long b11_rets;
long b11_to_r;
long b11_from_r;
long b11_fetch_r;
#endif
int b3_bad_opcode;

/* memory-mapped registers live inside M */
#define IP (M[REG_IP])
#define SP (M[REG_SP])
#define RP (M[REG_RP])
#define HP (M[REG_HP])

/* data-stack access via the memory-mapped SP */
#define push(v) do { M[--SP] = (v); } while (0)
#define pop()   (M[SP++])

/* --- memory layout (all disjoint) ---------------------------------------
 *   0..255          registers, scratch, host variables (small)
 *   CODE_BASE..     assembled code, grows up
 *   DS_INIT         main data stack top, grows down
 *   RS_INIT         main return stack top, grows down
 *   HEAP_BASE..     heap (HOST_ALLOC), grows up
 * (Generator stack regions are reserved by the tests, above the heap.) */
#define CODE_BASE  256
#define DS_INIT    16384
#define RS_INIT    24576
#define HEAP_BASE  32768

#ifdef B3_COUNT
void b11_reset_counts(void) {
    b11_total_ops = 0; b11_calls = 0; b11_rets = 0; b11_to_r = 0; b11_from_r = 0; b11_fetch_r = 0; b11_to_r = 0; b11_from_r = 0; b11_fetch_r = 0;
}
void b3_reset_counts(void) {
    b3_insns = 0;
    b3_min_sp = DS_INIT;
    b3_min_rp = RS_INIT;
    b3_bad_opcode = 0;
    for (int i = 0; i < 6; i++) b9_op_count[i] = 0;
    b11_total_ops = b11_calls = b11_rets = 0;
}
#endif

/* scratch cells used only by the derived SWAP macro */
enum { SC_A = 8, SC_B = 9 };

/* --- assembler ---------------------------------------------------------- */
static cell here;

void asm_reset(void) { here = CODE_BASE; }
cell asm_here(void)  { return here; }

static void emit(cell c) { M[here++] = c; }

void asm_lit(cell v) { emit(OP_LIT); emit(v); }
void asm_dup(void)   { emit(OP_DUP); }
void asm_drop(void)  { emit(OP_DROP); }
void asm_fetch(void) { emit(OP_FETCH); }
void asm_store(void) { emit(OP_STORE); }
void asm_halt(void)  { emit(OP_HALT); }
void asm_host(int id){ emit(OP_HOST); emit(id); }

void asm_add(void){ asm_host(HOST_ADD); } void asm_sub(void){ asm_host(HOST_SUB); }
void asm_mul(void){ asm_host(HOST_MUL); } void asm_div(void){ asm_host(HOST_DIV); }
void asm_mod(void){ asm_host(HOST_MOD); } void asm_neg(void){ asm_host(HOST_NEG); }
void asm_eq(void) { asm_host(HOST_EQ);  } void asm_ne(void){ asm_host(HOST_NE);  }
void asm_lt(void) { asm_host(HOST_LT);  } void asm_gt(void){ asm_host(HOST_GT);  }
void asm_le(void) { asm_host(HOST_LE);  } void asm_ge(void){ asm_host(HOST_GE);  }

cell asm_lit_fwd(void) {
    emit(OP_LIT);
    cell p = here;
    emit(0);
    return p;
}
cell asm_zbranch_fwd(void) {
    emit(OP_ZBRANCH);
    cell p = here;
    emit(0);
    return p;
}
cell asm_branch_fwd(void) {
    emit(OP_LIT);
    cell p = here;
    emit(0);
    emit(OP_LIT); emit(REG_IP); emit(OP_STORE);
    return p;
}
void asm_patch_here(cell p) { M[p] = here; }
void asm_zbranch(cell t)    { emit(OP_ZBRANCH); emit(t); }
void asm_branch(cell t)     { asm_lit(t); asm_lit(REG_IP); asm_store(); }

/* --- derived words ------------------------------------------------------ */

/* SWAP ( a b -- b a ) using two fixed scratch cells.
 * Non-reentrant (shared scratch), documented in RESULTS.md. */
void asm_swap(void) {
    asm_lit(SC_B); asm_store();   /* M[SC_B] = b */
    asm_lit(SC_A); asm_store();   /* M[SC_A] = a */
    asm_lit(SC_B); asm_fetch();   /* b */
    asm_lit(SC_A); asm_fetch();   /* b a */
}

/* >R ( x -- ) : rp--; M[rp] = x */
void asm_toR(void) {
    emit(B3_OP_TO_R);
}

/* R> ( -- x ) : x = M[rp]; rp++ */
void asm_fromR(void) {
    emit(B3_OP_FROM_R);
}

/* R@ ( -- x ) : peek top of return stack */
void asm_fetchR(void) {
    emit(B3_OP_R_FETCH);
}

/* B11: native RET replaces the macro EXIT. */
void asm_exit(void) {
    emit(B3_OP_RET);
}

/* B11: emit the B9 native CALL (two cells) and RET (one cell) instead of
 * the frozen macro expansion. The continuation is implicit in the return
 * stack entry, so no continuation literal is emitted. */
void asm_call(cell callee) {
    emit(B3_OP_CALL);
    emit(callee);
}

/* --- B2 native representatives ------------------------------------------ */
cell b3_native_call_fwd(void) {
    emit(B3_OP_CALL);
    cell p = here;
    emit(0);
    return p;
}
cell b3_native_branch_fwd(void) {
    emit(B3_OP_BRANCH);
    cell p = here;
    emit(0);
    return p;
}
void b3_native_ret(void)     { emit(B3_OP_RET); }
void b3_native_toR(void)     { emit(B3_OP_TO_R); }
void b3_native_fromR(void)   { emit(B3_OP_FROM_R); }
void b3_native_fetchR(void)  { emit(B3_OP_R_FETCH); }

/* --- host functions (no control-flow: only arithmetic/comparison/allocation/
 *       console I/O/diagnostics) ---------------------------------------- */
static void host(cell id) {
    switch (id) {
    case HOST_ADD: { cell b = pop(); cell a = pop(); push(a + b); break; }
    case HOST_SUB: { cell b = pop(); cell a = pop(); push(a - b); break; }
    case HOST_MUL: { cell b = pop(); cell a = pop(); push(a * b); break; }
    case HOST_DIV: { cell b = pop(); cell a = pop(); push(a / b); break; }
    case HOST_MOD: { cell b = pop(); cell a = pop(); push(a % b); break; }
    case HOST_NEG: { cell v = pop(); push(-v); break; }
    case HOST_EQ:  { cell b = pop(); cell a = pop(); push(a == b ? 1 : 0); break; }
    case HOST_NE:  { cell b = pop(); cell a = pop(); push(a != b ? 1 : 0); break; }
    case HOST_LT:  { cell b = pop(); cell a = pop(); push(a <  b ? 1 : 0); break; }
    case HOST_GT:  { cell b = pop(); cell a = pop(); push(a >  b ? 1 : 0); break; }
    case HOST_LE:  { cell b = pop(); cell a = pop(); push(a <= b ? 1 : 0); break; }
    case HOST_GE:  { cell b = pop(); cell a = pop(); push(a >= b ? 1 : 0); break; }
    case HOST_ALLOC: {
        cell n = pop(); cell a = HP; HP += n; push(a);
        break;
    }
    case HOST_PUTCHAR: { putchar((int)pop()); fflush(stdout); break; }
    case HOST_PRINT:   { printf("%ld\n", (long)pop()); fflush(stdout); break; }
    case HOST_DUMP: {
        fprintf(stderr, "[dump] IP=%ld SP=%ld RP=%ld HP=%ld top=%ld\n",
                (long)IP, (long)SP, (long)RP, (long)HP, (long)M[SP]);
        break;
    }
    default:
        fprintf(stderr, "s1: bad host id %ld\n", (long)id);
        break;
    }
}

/* --- machine ------------------------------------------------------------ */
void s1_reset(void) {
    SP = DS_INIT;
    RP = RS_INIT;
    HP = HEAP_BASE;
    IP = 0;
}

void s1_run(cell start) {
    IP = start;
    for (;;) {
#ifdef B3_COUNT
        b3_insns++;
        if (SP < b3_min_sp) b3_min_sp = SP;
        if (RP < b3_min_rp) b3_min_rp = RP;
#endif
#ifdef B11_COUNT
        b11_total_ops++;
#endif
        cell op = M[IP++];
        switch (op) {
        case OP_LIT:     push(M[IP++]); break;
        case OP_DUP:    { cell v = M[SP]; push(v); break; }
        case OP_DROP:    SP++; break;
        case OP_FETCH:  { cell a = pop(); cell v = M[a]; push(v); break; }
        case OP_STORE:  { cell a = pop(); cell v = pop(); M[a] = v; break; }
        case OP_ZBRANCH:{ cell f = pop(); cell t = M[IP++]; if (f == 0) IP = t; break; }
        case OP_HOST:    host(M[IP++]); break;
        case OP_HALT:    return;
        case B3_OP_CALL: {
#ifdef B3_COUNT
            b9_op_count[0]++;
#endif
#ifdef B11_COUNT
            b11_calls++;
#endif
            cell target = M[IP++]; M[--RP] = IP; IP = target; break; }
        case B3_OP_RET:
#ifdef B3_COUNT
            b9_op_count[1]++;
#endif
#ifdef B11_COUNT
            b11_rets++;
#endif
            IP = M[RP++]; break;
        case B3_OP_TO_R:
#ifdef B3_COUNT
            b9_op_count[2]++;
#endif
#ifdef B11_COUNT
            b11_to_r++;
#endif
            M[--RP] = pop(); break;
        case B3_OP_FROM_R:
#ifdef B3_COUNT
            b9_op_count[3]++;
#endif
#ifdef B11_COUNT
            b11_from_r++;
#endif
            push(M[RP++]); break;
        case B3_OP_R_FETCH:
#ifdef B3_COUNT
            b9_op_count[4]++;
#endif
#ifdef B11_COUNT
            b11_fetch_r++;
#endif
            push(M[RP]); break;
        case B3_OP_BRANCH:
#ifdef B3_COUNT
            b9_op_count[5]++;
#endif
            IP = M[IP]; break;
        default:
            b3_bad_opcode = 1;
            fprintf(stderr, "b2: bad opcode %ld at %ld\n", (long)op, (long)(IP - 1));
            return;
        }
    }
}

cell s1_top(void)              { return M[SP]; }
cell s1_mem(cell a)            { return M[a]; }
void s1_set_mem(cell a, cell v){ M[a] = v; }
