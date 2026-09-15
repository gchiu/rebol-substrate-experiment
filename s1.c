/* s1.c - S1 machine: flat cell memory, memory-mapped registers, two stacks,
 * a conditional branch, and a host escape. See s1.h for the architecture. */
#include "s1.h"

#include <stdio.h>

cell M[MEM_CELLS];

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
    asm_lit(REG_RP); asm_fetch(); /* x rp */
    asm_lit(1); asm_sub();        /* x rp-1 */
    asm_lit(REG_RP); asm_store(); /* rp = rp-1 ; x */
    asm_lit(REG_RP); asm_fetch(); /* x rp */
    asm_store();                  /* M[rp] = x */
}

/* R> ( -- x ) : x = M[rp]; rp++ */
void asm_fromR(void) {
    asm_lit(REG_RP); asm_fetch(); asm_fetch(); /* x */
    asm_lit(REG_RP); asm_fetch(); asm_lit(1); asm_add(); /* x rp+1 */
    asm_lit(REG_RP); asm_store();              /* rp = rp+1 ; x */
}

/* R@ ( -- x ) : peek top of return stack */
void asm_fetchR(void) {
    asm_lit(REG_RP); asm_fetch(); asm_fetch();
}

/* EXIT : ip = pop(R) */
void asm_exit(void) {
    asm_fetchR();                                /* retaddr */
    asm_lit(REG_RP); asm_fetch(); asm_lit(1); asm_add();
    asm_lit(REG_RP); asm_store();                /* rp++ */
    asm_lit(REG_IP); asm_store();                /* ip = retaddr */
}

/* CALL callee : R.push(continuation); ip = callee.
 * The continuation (address right after this expansion) is emitted as a
 * literal and patched to here once the expansion is complete. */
void asm_call(cell callee) {
    emit(OP_LIT);
    cell p = here;
    emit(0);                    /* continuation placeholder */
    asm_toR();                  /* push continuation onto R */
    asm_lit(callee); asm_lit(REG_IP); asm_store(); /* ip = callee */
    M[p] = here;                /* continuation = address after this call */
}

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
        default:
            fprintf(stderr, "s1: bad opcode %ld at %ld\n", (long)op, (long)(IP - 1));
            return;
        }
    }
}

cell s1_top(void)              { return M[SP]; }
cell s1_mem(cell a)            { return M[a]; }
void s1_set_mem(cell a, cell v){ M[a] = v; }
