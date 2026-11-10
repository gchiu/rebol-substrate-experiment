/* r0_s1_masm_tests.c - the MASM surface: `masm [...]` is the canonical
 * spelling of the S1 macroassembler; `raw` is retained as a compatibility
 * alias that enters the exact same assembler path.
 *
 * These tests prove the surface, not the machine: a trivial sequence
 * assembles and runs, `masm` and `raw` agree, labels work, symbolic
 * registers/mnemonics work, and the HOST/PRINT forms reach the host
 * dispatcher. The frozen S1 substrate is untouched.
 *
 * Tests 8-13 (MASM-PRIVATE-NAMES) prove that masm is a private sublanguage:
 * its mnemonics, operands and labels never enter the Glon symbol table and
 * never interact with Glon lexical binding, and the code it emits is
 * unchanged.
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

/* evaluate a program that leaves exactly one value; -99999 otherwise */
static int one(const char *prog) { eval(prog); return (N == 1) ? got(0) : -99999; }

static int nsyms_now(void) { int k = 0; while (r0_s1_sym_name(k)) k++; return k; }
static int has_sym(const char *name) {
    for (int k = 0; r0_s1_sym_name(k); k++) if (strcmp(r0_s1_sym_name(k), name) == 0) return 1;
    return 0;
}

/* Relocation-normalised checksum of the code masm emits for one source file.
 *
 * The emitted S1 code contains absolute addresses, so a raw checksum changes
 * whenever the emitted runtime grows, although masm's output is the same. The
 * stream is decoded exactly (only LIT, 0BRANCH and HOST carry an inline
 * operand), and each operand is classified:
 *   - an address inside this file's own emission (a branch target, label or
 *     call continuation) is hashed as its OFFSET from the emission start;
 *   - a LIT equal to one of the runtime entry points masm resolves by name
 *     (CALL alloc / collect / lookup / ESC_ANY / ESC_TRANSPORT) is hashed as
 *     that NAME. The addresses are found by assembling a one-line probe,
 *     `masm 0 [ CALL name ]`, with the same assembler;
 *   - every other operand (data, registers, scratch cells, HOST ids, a numeric
 *     CALL target) and every opcode is hashed EXACTLY.
 * So relocation is the only difference it ignores. A changed opcode, literal,
 * branch target or callee still changes the checksum. */
static const char *masm_routines[] = { "alloc", "collect", "lookup", "ESC_ANY", "ESC_TRANSPORT" };
#define N_MASM_ROUTINES 5
static cell masm_routine_addr[N_MASM_ROUTINES];

static cell probe_routine(const char *name) {
    char p[96];
    /* EXIT keeps the probe a well-formed block under K7: a CALL returns to
     * the instruction after it, which would otherwise be the end of code. */
    snprintf(p, sizeof p, "[ x: masm 0 [ CALL %s EXIT ] ]", name);
    r0_s1_init();
    cell a = asm_here();
    int err = 0;
    r0_s1_parse(p, &err);
    cell b = asm_here();
    if (err) return -1;
    /* a derived call ends ... LIT callee LIT REG_IP ! ; the callee lies outside the probe */
    cell prev = -1, last = -1;
    for (cell i = a; i < b; i++) {
        cell op = s1_mem(i);
        if (op == OP_LIT) { prev = last; last = s1_mem(++i); continue; }
        if (op == OP_ZBRANCH || op == OP_HOST) { i++; last = -1; continue; }
        if (op == OP_STORE && last == REG_IP && prev >= 256 && (prev < a || prev > b)) return prev;
        last = -1;
    }
    return -1;
}

static unsigned fnv_h;
static void fnv_mix(unsigned long long v) {
    for (int k = 0; k < 8; k++) { fnv_h ^= (unsigned)((v >> (8 * k)) & 255u); fnv_h *= 16777619u; }
}

/* Parse one source file on a fresh session (comments stripped, as glon_load
 * does) and checksum the code emitted during the parse (the masm assembly
 * alone), normalised for relocation as described above. */
static int masm_emission(const char *path, long *cells, unsigned *fnv) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    size_t n = fread(src, 1, sizeof src - 1, fp);
    fclose(fp);
    src[n] = 0;
    char *r = src, *w = src;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    r0_s1_init();
    cell a = asm_here();
    int err = 0;
    r0_s1_parse(src, &err);
    cell b = asm_here();
    if (err) return 0;
    fnv_h = 2166136261u;
    for (cell i = a; i < b; i++) {
        cell op = s1_mem(i);
        fnv_mix(0); fnv_mix((unsigned long long)op);                 /* opcode: exact */
        if (op != OP_LIT && op != OP_ZBRANCH && op != OP_HOST) continue;
        if (++i >= b) return 0;                                       /* truncated operand */
        cell v = s1_mem(i);
        if (op != OP_HOST && v >= a && v <= b) { fnv_mix(1); fnv_mix((unsigned long long)(v - a)); continue; }
        int named = 0;
        for (int k = 0; op == OP_LIT && k < N_MASM_ROUTINES; k++)
            if (v == masm_routine_addr[k]) { fnv_mix(2); fnv_mix((unsigned long long)k); named = 1; break; }
        if (!named) { fnv_mix(3); fnv_mix((unsigned long long)v); }  /* operand: exact */
    }
    *cells = (long)(b - a);
    *fnv = fnv_h;
    return 1;
}

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

    /* ---- MASM-PRIVATE-NAMES: masm is a private sublanguage. Its mnemonics,
     * operands, symbolic registers and labels are read by the assembler's own
     * tokenizer; they never enter the Glon symbol table and never interact
     * with Glon lexical binding. Each check starts a fresh session. */

    /* 8: masm-only spellings consume no Glon symbols. Only the binding name
     * (a set-word outside the masm block) is interned. */
    r0_s1_init();
    {
        int before = nsyms_now();
        eval("[ zq-m8: masm 1 [ DUP ZBRANCH zq-l8-end LIT SCRATCH_E ! LIT SCRATCH_E @ zq-l8-end: ARITY 1 EXIT ] ]");
        int added = nsyms_now() - before;
        const char *masm_only[] = { "DUP", "ZBRANCH", "zq-l8-end", "LIT", "SCRATCH_E", "!", "@",
                                    "ARITY", "EXIT", NULL };
        int leaked = 0;
        for (int i = 0; masm_only[i]; i++) if (has_sym(masm_only[i])) leaked++;
        CHECK(N >= 0 && added == 1 && has_sym("zq-m8") && leaked == 0 && one("[ zq-m8 4 ]") == 4,
              "8: masm-only words consume no Glon symbols (only the binding name is interned)");
    }

    /* 9: a masm mnemonic, operand or label may share its spelling with an
     * ordinary Glon word (here: globals DUP, ADD, Lx and SCRATCH_A) without
     * either side seeing the other. */
    r0_s1_init();
    eval("[ DUP: 5  ADD: func [a b] [ - a b ]  Lx: 100  SCRATCH_A: 9"
         "  f9: masm 1 [ DUP ADD ARITY 1 EXIT ]"
         "  g9: masm 0 [ BRANCH Lx LIT 16 Lx: LIT 32 LIT SCRATCH_A ! LIT SCRATCH_A @ ARITY 1 EXIT ] ]");
    CHECK(N >= 0 && one("[ f9 3 ]") == 6 && one("[ g9 ]") == 2 && one("[ DUP ]") == 5
          && one("[ ADD 10 4 ]") == 6 && one("[ Lx ]") == 100 && one("[ SCRATCH_A ]") == 9,
          "9: masm opcode/operand/label spellings coexist with same-named Glon words");

    /* 10: inside a func body, masm tokens that share a spelling with the
     * func's parameters are not lexically resolved (no T_BOUND): LIT and ADD
     * as mnemonics, SCRATCH_A as an operand, Lq as a branch label. */
    r0_s1_init();
    eval("[ h10: func [LIT ADD] [ k: masm 1 [ LIT 16 ADD ARITY 1 EXIT ]  k 41 ]"
         "  s10: func [SCRATCH_A] [ k: masm 1 [ LIT SCRATCH_A ! LIT SCRATCH_A @ ARITY 1 EXIT ]  k 9 ]"
         "  b10: func [Lq n] [ k: masm 1 [ DUP ZBRANCH Lq LIT 16 ADD Lq: ARITY 1 EXIT ]  k n ] ]");
    CHECK(N >= 0 && one("[ h10 7 8 ]") == 42 && one("[ s10 1 ]") == 9
          && one("[ b10 0 5 ]") == 6 && one("[ b10 0 0 ]") == 0 && r0_s1_ran_cleanly(),
          "10: masm tokens named like func parameters are not lexically bound");

    /* 11: labels resolve within their own block; two blocks may reuse the
     * same label names independently (forward and backward branches). */
    r0_s1_init();
    eval("[ a11: masm 0 [ LIT 0 L: DUP LIT 48 LT ZBRANCH D LIT 16 ADD BRANCH L D: ARITY 1 EXIT ]"
         "  b11: masm 0 [ LIT 0 L: DUP LIT 80 LT ZBRANCH D LIT 16 ADD BRANCH L D: ARITY 1 EXIT ] ]");
    CHECK(N >= 0 && one("[ a11 ]") == 3 && one("[ b11 ]") == 5,
          "11: labels resolve per block; same label names in two blocks are independent");

    /* 12: after masm blocks have used the spellings L, D, LIT and ADD, the
     * same spellings behave as ordinary Glon words, set-words and parameters. */
    eval("[ L: 7  D: func [x] [ * x 2 ]  p12: func [LIT ADD] [ + LIT ADD ] ]");
    CHECK(N >= 0 && one("[ L ]") == 7 && one("[ D 21 ]") == 42 && one("[ p12 3 4 ]") == 7
          && has_sym("L") && has_sym("D"),
          "12: ordinary Glon words with masm spellings behave normally outside masm");

    /* 13: the code masm emits for the shipped libraries is identical to the
     * pre-fix assembler, up to relocation (see masm_emission). The reference
     * values were computed with this same normalised checksum by the
     * unmodified runtime at bdcfa9d (the assembler before MASM-PRIVATE-NAMES).
     * The previous HEAD (0011803), and the runtime with the GC worklist bound,
     * which grew the emitted runtime by 117 cells, give the same values. */
    {
        int probes_ok = 1;
        for (int k = 0; k < N_MASM_ROUTINES; k++)
            if ((masm_routine_addr[k] = probe_routine(masm_routines[k])) < 0) probes_ok = 0;
        CHECK(probes_ok, "13a: masm's named runtime entry points (alloc, collect, lookup, ESC_ANY, ESC_TRANSPORT) are located");
        static const struct { const char *path; long cells; unsigned fnv; } lib[] = {
            { "demo/shop/bootstrap.glon",             802, 0xf7eaead6u },
            /* common.glon is unwrapped (no outer [ ]). Until the K4 repair
             * (2026-11-10) masm was recognised only inside a block, so its
             * top-level masm definitions were not assembled (147 cells,
             * 0x8c74acee); now it emits exactly what its wrapped bundle,
             * bootstrap.glon above, emits. */
            { "demo/shop/common.glon",                802, 0xf7eaead6u },
            { "demo/shop/strings.glon",               304, 0x7a334bcfu },
            { "demo/shop/demos/linda.glon",          1969, 0xb11d2c08u },
            { "demo/shop/demos/merchant-flow.glon",  1017, 0x8711349bu },
            { "demo/shop/demos/tuple-space.glon",    1125, 0xd1ae8fd9u },
            { "demo/shop/demos/traffic.glon",          55, 0xd353a2c0u },
            { "demos/kaka/kaka-lib.glon",             988, 0xfcaaab6bu },
            { "demos/patrol/patrol.glon",             285, 0xede1a66au },
            { "jupyter/prelude.glon",                 416, 0xf02b5d2du },
            { "desktop/emit.glon",                    193, 0x74e29708u },
            { "standalone/app.glon",                   63, 0xf0dc9b01u },
            { NULL, 0, 0 }
        };
        int ok = 1, seen = 0;
        for (int i = 0; lib[i].path; i++) {
            long cells = 0; unsigned fnv = 0;
            if (!masm_emission(lib[i].path, &cells, &fnv)) { printf("  13: cannot parse %s\n", lib[i].path); ok = 0; continue; }
            seen++;
            if (cells != lib[i].cells || fnv != lib[i].fnv) {
                printf("  13: %s emitted %ld cells fnv %08x, expected %ld %08x\n",
                       lib[i].path, cells, fnv, lib[i].cells, lib[i].fnv);
                ok = 0;
            }
        }
        CHECK(ok && seen == 12 && probes_ok, "13: masm-emitted code for the shipped libraries is identical to the pre-fix assembler (up to relocation)");
    }

    if (failures == 0) printf("all MASM-surface tests passed\n");
    return failures;
}
