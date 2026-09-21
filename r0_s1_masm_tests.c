/* r0_s1_masm_tests.c - the MASM surface: `masm [...]` is the canonical
 * spelling of the S1 macroassembler; `raw` is retained as a compatibility
 * alias that enters the exact same assembler path.
 *
 * These tests prove the surface, not the machine: a trivial sequence
 * assembles and runs, `masm` and `raw` agree, labels work, symbolic
 * registers/mnemonics work, and the HOST/PRINT forms reach the host
 * dispatcher. The frozen S1 substrate is untouched.
 */

#include "r0_s1.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static char src[65536];

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static int N;
static void eval(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { N = -1; return; }
    N = r0_s1_run_persistent(b);
}
static int got(int i) { return (i < N) ? (int)int_val(r0_s1_result(i, N)) : 0; }

int run_r0_s1_masm_tests(void) {
    r0_s1_init();

    printf("R0-S1 MASM surface (canonical macroassembler spelling)\n");

    /* trivial sequence under masm: increment the tagged argument */
    eval("[ f: masm 1 [ LIT 16 ADD ARITY 1 EXIT ]  f 41 ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "1: masm assembles + runs a trivial sequence (41+1=42)");

    /* raw is the same assembler: identical block, identical result */
    eval("[ f: raw 1 [ LIT 16 ADD ARITY 1 EXIT ]  f 41 ]");
    CHECK(N == 1 && got(0) == 42, "2: raw alias produces equivalent behaviour (41+1=42)");

    /* labels + forward branch + backward branch: count 0..3 */
    eval("[ g: masm 0 [ LIT 0  Lloop: DUP LIT 48 LT ZBRANCH Ldone LIT 16 ADD BRANCH Lloop  Ldone: ARITY 1 EXIT ]  g ]");
    CHECK(N == 1 && got(0) == 3, "3: labels + ZBRANCH/BRANCH loop under masm (counts to 3)");

    /* symbolic register names: store then load through SCRATCH_A */
    eval("[ h: masm 1 [ LIT SCRATCH_A ! LIT SCRATCH_A @ ARITY 1 EXIT ]  h 7 ]");
    CHECK(N == 1 && got(0) == 7, "4: symbolic register name (SCRATCH_A) resolves (7)");

    /* bare mnemonic arithmetic (HOST_SUB sugar) */
    eval("[ k: masm 2 [ SUB ARITY 1 EXIT ]  k 10 3 ]");
    CHECK(N == 1 && got(0) == 7, "5: bare SUB mnemonic (10-3=7)");

    /* explicit HOST form: HOST 0 == ADD */
    eval("[ m: masm 2 [ HOST 0 ARITY 1 EXIT ]  m 20 22 ]");
    CHECK(N == 1 && got(0) == 42, "6: explicit HOST opcode form (HOST 0 == ADD)");

    /* PRINT sugar reaches the host dispatcher */
    eval("[ p: masm 0 [ LIT 65 PRINT ARITY 0 EXIT ]  p ]");
    CHECK(N == 0 && r0_s1_ran_cleanly(), "7: PRINT sugar reaches the host dispatcher (arity 0)");

    if (failures == 0) printf("all MASM-surface tests passed\n");
    return failures;
}
