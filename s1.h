/* s1.h - minimal S1 Forth-like substrate (portable C17).
 *
 * Architecture: flat cell-addressed memory, memory-mapped machine
 * registers (IP, SP, RP, HP), a data stack and a return/control stack.
 *
 * The only irreducible primitives are: LIT, DUP, DROP, @, !, 0BRANCH, HOST.
 * Everything else (SWAP, >R, R>, R@, CALL, EXIT, BRANCH) is DERIVED as
 * assembler macros over those primitives. OP_HALT is a harness detail that
 * stops the machine; it does not manipulate state.
 *
 * The primitive instruction set is FROZEN. See RESULTS.md.
 */
#ifndef S1_H
#define S1_H

#include <stdint.h>

typedef intptr_t cell;

#define MEM_CELLS 65536

/* memory-mapped machine registers */
enum {
    REG_IP = 0,
    REG_SP = 1,
    REG_RP = 2,
    REG_HP = 3
};

/* --- FROZEN primitive instruction set -------------------------------- */
enum {
    OP_LIT = 0,   /* LIT v      -- v        push inline literal */
    OP_DUP,       /* DUP        x -- x x    duplicate top */
    OP_DROP,      /* DROP       x --        discard top */
    OP_FETCH,     /* @          a -- M[a]   read memory (incl. registers) */
    OP_STORE,     /* !          v a --      write memory (incl. registers) */
    OP_ZBRANCH,   /* 0BRANCH    f --        pop flag; if 0, branch to inline addr */
    OP_HOST,      /* HOST id    --          host escape (inline native id) */
    OP_HALT       /* machine stop (harness; not a control primitive) */
};

/* host function ids: arithmetic / comparison / allocation / I/O / diag only.
 * Host functions MUST NOT manipulate IP, SP, RP, unwind, or save/restore
 * execution state. Those behaviours are built from the machine primitives. */
enum {
    HOST_ADD = 0, HOST_SUB, HOST_MUL, HOST_DIV, HOST_MOD, HOST_NEG,
    HOST_EQ, HOST_NE, HOST_LT, HOST_GT, HOST_LE, HOST_GE,
    HOST_ALLOC, HOST_PUTCHAR, HOST_PRINT, HOST_DUMP
};

/* --- assembler API ----------------------------------------------------- */
void  asm_reset(void);             /* reset assembler cursor */
cell  asm_here(void);              /* current code address */

void  asm_lit(cell v);
void  asm_dup(void);
void  asm_drop(void);
void  asm_fetch(void);             /* @ */
void  asm_store(void);             /* ! */
void  asm_halt(void);
void  asm_host(int id);

/* arithmetic/comparison conveniences (expand to HOST) */
void  asm_add(void); void asm_sub(void); void asm_mul(void);
void  asm_div(void); void asm_mod(void); void asm_neg(void);
void  asm_eq(void);  void asm_ne(void);  void asm_lt(void);
void  asm_gt(void);  void asm_le(void);  void asm_ge(void);

/* branches (0BRANCH is a primitive; BRANCH is derived) */
cell  asm_lit_fwd(void);           /* emit LIT with placeholder; return operand addr */
cell  asm_zbranch_fwd(void);       /* emit 0BRANCH; return placeholder operand addr */
cell  asm_branch_fwd(void);        /* emit derived BRANCH; return placeholder addr */
void  asm_patch_here(cell p);      /* patch placeholder to current here */
void  asm_zbranch(cell target);    /* 0BRANCH to known target */
void  asm_branch(cell target);     /* derived BRANCH to known target */

/* derived words (macros over the frozen primitives) */
void  asm_swap(void);              /* ( a b -- b a ) via two scratch cells */
void  asm_toR(void);               /* >R ( x -- ) */
void  asm_fromR(void);             /* R> ( -- x ) */
void  asm_fetchR(void);            /* R@ ( -- x ) peek */
void  asm_call(cell callee);       /* CALL: R.push(continuation); ip = callee */
void  asm_exit(void);              /* EXIT: ip = pop(R) */

/* --- machine API -------------------------------------------------------- */
void  s1_reset(void);              /* reset SP/RP/HP (leaves code) */
void  s1_run(cell start);          /* run from start until OP_HALT */
cell  s1_top(void);                /* top of data stack */
cell  s1_mem(cell addr);           /* read a memory cell */
void  s1_set_mem(cell addr, cell v); /* write a memory cell */

#endif /* S1_H */
