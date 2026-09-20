/* r0_s1_debug_tests.c - D1 cooperative debugger: HLL-first, RAW trapdoor only.
 *
 * The debugger's POLICY and PRESENTATION are ordinary R0 source. The only
 * low-level work (capturing / inspecting / restoring machine state) goes
 * through the generic RAW/S1 trapdoor. The R0 evaluator has no knowledge that
 * debugging exists.
 *
 * Mechanism summary
 * -----------------
 * The debugger and the debuggee are two "worlds" swapped in the SAME S1 run.
 * Two 9-cell state records (a fixed free-region buffer each) hold
 *   [ resume_ip, SP, RP, HP, RV_CUR, RV_END, RV_CTX, RV_BLK, RV_FRAME ].
 *   - `continue` (RAW): save current (debugger) state -> record B, then
 *     restore record A (debuggee) and jump to its resume_ip.  It also first
 *     copies the live HP into record A's HP slot so the two worlds never
 *     allocate over each other (monotonic heap).
 *   - `debug-break` (RAW): save current (debuggee) state -> record A, restore
 *     record B (debugger), push [handle, 1] as the value returned by the
 *     suspended `continue`, then jump to the debugger resume_ip.
 *
 * The C harness only: emits nothing, parses source, seeds record A with the
 * debuggee's INITIAL machine state (exactly what r0_s1_run would install), and
 * runs the debugger block.  No debugger logic lives in C.
 */

#include "r0_s1.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

/* fixed free-region state buffers (in the M3C-relocated state region
 * [24576, 25315), just above the return-stack top RS_INIT=24576; the debuggee's
 * stacks live below that, at DBGEE_SP=12000 / DBGEE_RP=20000) */
#define DBGEE_BUF 24859
#define DBGER_BUF 24879

/* the debuggee runs on a reserved lower stack region so its return stack can
 * never overwrite the debugger's live return stack (24576 downward). */
#define DBGEE_SP 12000
#define DBGEE_RP 20000

/* the debugger runs on its own heap region (>= 50000), disjoint from the
 * debuggee's S1 heap (32768..40000) and the loader heap (40000..50600).  This
 * is what keeps the two worlds' allocations from overwriting each other; the
 * small debuggee programs never push the loader heap into the debugger's
 * 50000+ region. */
#define DBGER_HP 50000

/* ---- the debugger library: RAW mechanism primitives --------------------- */
static const char *DBG_LIB =
    /* establish the debugger's own heap region BEFORE any closure is defined
     * (ordinary R0 closures created during inspection allocate here, not over
     * the debuggee's live frames/contexts) */
    " set-hp: raw [ LIT 50000 LIT REG_HP ! ARITY 0 EXIT ] set-hp "
    /* cooperative breakpoint: debuggee -> debugger, push [handle,1], jump */
    " debug-break: raw [ "
    "   NONE LIT 16 "                                         /* debug-break's own result: [NONE,1] */
    "   LIT REG_RP @ @ LIT SCRATCH_A @ LIT 0 ADD ! "
    "   LIT REG_SP @ LIT SCRATCH_A @ LIT 1 ADD ! "
    "   LIT REG_RP @ LIT SCRATCH_A @ LIT 2 ADD ! "
    "   LIT REG_HP @ LIT SCRATCH_A @ LIT 3 ADD ! "
    "   LIT RV_CUR @ LIT SCRATCH_A @ LIT 4 ADD ! "
    "   LIT RV_END @ LIT SCRATCH_A @ LIT 5 ADD ! "
    "   LIT RV_CTX @ LIT SCRATCH_A @ LIT 6 ADD ! "
    "   LIT RV_BLK @ LIT SCRATCH_A @ LIT 7 ADD ! "
    "   LIT RV_FRAME @ LIT SCRATCH_A @ LIT 8 ADD ! "
    "   LIT SCRATCH_B @ LIT 1 ADD @ LIT REG_SP ! "
    "   LIT SCRATCH_B @ LIT 2 ADD @ LIT REG_RP ! "
    "   LIT SCRATCH_B @ LIT 3 ADD @ LIT REG_HP ! "
    "   LIT SCRATCH_B @ LIT 4 ADD @ LIT RV_CUR ! "
    "   LIT SCRATCH_B @ LIT 5 ADD @ LIT RV_END ! "
    "   LIT SCRATCH_B @ LIT 6 ADD @ LIT RV_CTX ! "
    "   LIT SCRATCH_B @ LIT 7 ADD @ LIT RV_BLK ! "
    "   LIT SCRATCH_B @ LIT 8 ADD @ LIT RV_FRAME ! "
    "   LIT SCRATCH_A @ LIT 16 MUL "
    "   LIT 16 "
    "   LIT SCRATCH_B @ LIT 0 ADD @ LIT REG_IP ! ] "
    /* resume: debugger -> debuggee */
    " continue: raw [ "
    "   LIT REG_RP @ @ LIT SCRATCH_B @ LIT 0 ADD ! "
    "   LIT REG_SP @ LIT SCRATCH_B @ LIT 1 ADD ! "
    "   LIT REG_RP @ LIT SCRATCH_B @ LIT 2 ADD ! "
    "   LIT REG_HP @ LIT SCRATCH_B @ LIT 3 ADD ! "
    "   LIT RV_CUR @ LIT SCRATCH_B @ LIT 4 ADD ! "
    "   LIT RV_END @ LIT SCRATCH_B @ LIT 5 ADD ! "
    "   LIT RV_CTX @ LIT SCRATCH_B @ LIT 6 ADD ! "
    "   LIT RV_BLK @ LIT SCRATCH_B @ LIT 7 ADD ! "
    "   LIT RV_FRAME @ LIT SCRATCH_B @ LIT 8 ADD ! "
    "   LIT SCRATCH_A @ LIT 1 ADD @ LIT REG_SP ! "
    "   LIT SCRATCH_A @ LIT 2 ADD @ LIT REG_RP ! "
    "   LIT SCRATCH_A @ LIT 3 ADD @ LIT REG_HP ! "
    "   LIT SCRATCH_A @ LIT 4 ADD @ LIT RV_CUR ! "
    "   LIT SCRATCH_A @ LIT 5 ADD @ LIT RV_END ! "
    "   LIT SCRATCH_A @ LIT 6 ADD @ LIT RV_CTX ! "
    "   LIT SCRATCH_A @ LIT 7 ADD @ LIT RV_BLK ! "
    "   LIT SCRATCH_A @ LIT 8 ADD @ LIT RV_FRAME ! "
    "   LIT SCRATCH_A @ LIT 0 ADD @ LIT REG_IP ! ] "
    /* inspection primitives (generic single-cell reads of the captured state;
     * the lexical chain-walk lives in the generic RAW fragment ctx-lookup) */
    " state-get: raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD @ ARITY 1 EXIT ] "
    " state-get-i: raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD @ LIT 16 MUL ARITY 1 EXIT ] "
    " frame-get: raw 2 [ LIT 16 DIV ADD @ ARITY 1 EXIT ] "
    " frame-get-i: raw 2 [ LIT 16 DIV ADD @ LIT 16 MUL ARITY 1 EXIT ] "
    " ctx-count: raw 1 [ DUP LIT 16 MOD SUB LIT 1 ADD @ LIT 16 MUL ARITY 1 EXIT ] "
    " ctx-parent: raw 1 [ DUP LIT 16 MOD SUB @ ARITY 1 EXIT ] "
    " ctx-word: raw 2 [ LIT 16 DIV >R DUP LIT 16 MOD SUB LIT 3 ADD R> LIT 2 MUL ADD @ ARITY 1 EXIT ] "
    " ctx-val: raw 2 [ LIT 16 DIV >R DUP LIT 16 MOD SUB LIT 4 ADD R> LIT 2 MUL ADD @ ARITY 1 EXIT ] "
    " ctx-lookup: raw 2 [ "
    "   LIT SCRATCH_A @ >R LIT SCRATCH_B @ >R "
    "   LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
    "   Lclkouter: "
    "   LIT SCRATCH_A @ LIT 0 EQ ZBRANCH Lclkok "
    "     R> LIT SCRATCH_B ! R> LIT SCRATCH_A ! LIT -1 ARITY 1 EXIT "
    "   Lclkok: "
    "   LIT 0 "
    "   Lclkin: "
    "   DUP LIT SCRATCH_A @ LIT 1 ADD @ GE ZBRANCH Lclksearch "
    "     DROP LIT SCRATCH_A @ @ DUP LIT 16 MOD SUB LIT SCRATCH_A ! BRANCH Lclkouter "
    "   Lclksearch: "
    "   DUP LIT 2 MUL LIT SCRATCH_A @ LIT 3 ADD ADD @ "
    "   LIT SCRATCH_B @ EQ ZBRANCH Lclknomatch "
    "     LIT 2 MUL LIT SCRATCH_A @ LIT 4 ADD ADD @ "
    "     R> LIT SCRATCH_B ! R> LIT SCRATCH_A ! ARITY 1 EXIT "
    "   Lclknomatch: "
    "   LIT 1 ADD BRANCH Lclkin ] "
    /* generic frame-chain depth counter (RAW walk; no R0 recursion needed) */
    " frame-depth: raw 1 [ "
    "   LIT SCRATCH_A @ >R "
    "   LIT SCRATCH_A ! "
    "   LIT 0 "
    "   Lfdouter: "
    "   LIT SCRATCH_A @ LIT 0 EQ ZBRANCH Lfdok "
    "     R> LIT SCRATCH_A ! LIT 16 MUL ARITY 1 EXIT "
    "   Lfdok: "
    "   LIT 1 ADD LIT SCRATCH_A @ @ LIT SCRATCH_A ! BRANCH Lfdouter ] ";

/* ---- harness: run a full debug session ---------------------------------- */
static char dbg_buf[16384];
static char ret_buf[512];

/* run the debugger body (DBG_LIB + body) against the given debuggee program.
 * Returns the debuggee's final result arity via *N (the values are the ones
 * r0_s1_result reports). Inspection values the debugger stored in global
 * words are retrieved afterwards with get_int(). */
static int debug_session(const char *debuggee, const char *body, int *N) {
    int err = 0;
    cell main_entry = r0_s1_init();

    snprintf(dbg_buf, sizeof dbg_buf, "[ %s %s ]", DBG_LIB, body);
    cell dbg_block = r0_s1_parse(dbg_buf, &err);
    if (err) { *N = -1; return 0; }
    cell gee_block = r0_s1_parse(debuggee, &err);
    if (err) { *N = -1; return 0; }

    /* capture the global context (static in the runtime) via a trivial run */
    cell dummy = r0_s1_parse("[ none ]", &err);
    int dn = r0_s1_run(dummy);
    (void)dn;
    cell gctx = M[RV_CTX];

    /* fresh machine bases (debugger world uses the standard stacks) */
    s1_reset();
    cell hp0 = s1_mem(REG_HP);

    /* seed the debuggee record with its INITIAL machine state (a reserved,
     * disjoint stack region) */
    cell bp = r0_untag(gee_block);
    M[DBGEE_BUF + 0] = main_entry;
    M[DBGEE_BUF + 1] = DBGEE_SP;
    M[DBGEE_BUF + 2] = DBGEE_RP;
    M[DBGEE_BUF + 3] = hp0;
    M[DBGEE_BUF + 4] = bp + BLK_DATA;
    M[DBGEE_BUF + 5] = bp + BLK_DATA + M[bp];
    M[DBGEE_BUF + 6] = gctx;
    M[DBGEE_BUF + 7] = bp;
    M[DBGEE_BUF + 8] = 0;
    M[RV_SCRATCH_A] = DBGEE_BUF;
    M[RV_SCRATCH_B] = DBGER_BUF;

    *N = r0_s1_run(dbg_block);
    return 0;
}

/* retrieve an integer the debugger bound in the global context */
static cell get_int(const char *word) {
    int err = 0;
    snprintf(ret_buf, sizeof ret_buf, "[ %s ]", word);
    cell b = r0_s1_parse(ret_buf, &err);
    if (err) return -123456;
    int N = r0_s1_run(b);
    return (N == 1) ? r0_s1_result(0, 1) : -123456;
}

static cell expect_session1(const char *debuggee, const char *body, cell want,
                            const char *what) {
    int N;
    debug_session(debuggee, body, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == want);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)want); failures++; }
    return (N == 1) ? r0_s1_result(0, 1) : -999;
}

/* ============================== the tests ================================ */

/* frame inspection uses the generic RAW frame-depth/frame-get primitives */

static void test_basic(void) {
    printf("debugger: A basic pause/continue\n");
    const char *gee = "[ f: func [] [ x: 10 debug-break x: + x 5 x ]  f ]";
    const char *body =
        " st: continue "
        " ctx: state-get st 6 "
        " dbg-x: ctx-lookup ctx 'x "
        " continue ";
    expect_session1(gee, body, mk_int(15), "A: result == 15 after continue");
    CHECK(get_int("dbg-x") == mk_int(10), "A: at breakpoint, x == 10 (via debugger-level inspection)");
}

static void test_locals(void) {
    printf("debugger: B locals / context\n");
    const char *gee = "[ f: func [a] [ b: + a 1 debug-break + b 10 ]  f 5 ]";
    const char *body =
        " st: continue "
        " ctx: state-get st 6 "
        " dbg-a: ctx-lookup ctx 'a "
        " dbg-b: ctx-lookup ctx 'b "
        " continue ";
    expect_session1(gee, body, mk_int(16), "B: result == 16 after continue");
    CHECK(get_int("dbg-a") == mk_int(5), "B: inspected a == 5");
    CHECK(get_int("dbg-b") == mk_int(6), "B: inspected b == 6");
}

static void test_frames(void) {
    printf("debugger: C call frames\n");
    const char *gee = "[ h: func [] [ debug-break 99 ]  g: func [] [ h ]  f: func [] [ g ]  f ]";
    const char *body =
        " st: continue "
        " fr: state-get st 8 "
        " dbg-depth: frame-depth fr "
        " h-site: frame-get-i fr 1 "
        " h-ip: frame-get-i fr 4 "
        " h-rp: frame-get-i fr 3 "
        " h-ctx: frame-get fr 5 "
        " continue ";
    expect_session1(gee, body, mk_int(99), "C: result == 99 after continue");
    CHECK(get_int("dbg-depth") == mk_int(3), "C: frame depth == 3 (f,g,h)");
    CHECK(get_int("h-site") != 0, "C: top frame has a real func site-id");
    CHECK(get_int("h-ip") != 0 && get_int("h-rp") != 0, "C: top frame records saved IP/RP");
}

static void test_unaware(void) {
    printf("debugger: D unaware callers\n");
    /* f/g/h contain no debugger-specific code; only h's body calls debug-break */
    const char *gee =
        "[ h: func [] [ debug-break 7 ] "
        "  g: func [] [ h ] "
        "  f: func [] [ g ] "
        "  f ]";
    const char *body =
        " st: continue "
        " dbg-resumed: 1 "
        " continue ";
    /* debug-break returns NONE (discarded); h returns 7 */
    expect_session1(gee, body, mk_int(7), "D: unaware chain pauses and resumes; h == 7");
}

static void test_side_effect(void) {
    printf("debugger: E side-effect boundary\n");
    const char *gee =
        "[ counter: 0 "
        "  f: func [] [ counter: + counter 1 debug-break counter: + counter 10 counter ] "
        "  f ]";
    const char *body =
        " st: continue "
        " dbg-before: counter "
        " continue ";
    expect_session1(gee, body, mk_int(11), "E: after continue, counter == 11");
    CHECK(get_int("dbg-before") == mk_int(1), "E: at breakpoint, counter == 1");
}

static void test_two_breakpoints(void) {
    printf("debugger: F two breakpoints\n");
    const char *gee =
        "[ counter: 0 "
        "  f: func [] [ counter: + counter 1 debug-break counter: + counter 10 debug-break counter: + counter 100 counter ] "
        "  f ]";
    const char *body =
        " st1: continue "
        " dbg-first: counter "
        " st2: continue "
        " dbg-second: counter "
        " continue ";
    expect_session1(gee, body, mk_int(111), "F: after two breaks, counter == 111");
    CHECK(get_int("dbg-first") == mk_int(1), "F: at breakpoint 1, counter == 1");
    CHECK(get_int("dbg-second") == mk_int(11), "F: at breakpoint 2, counter == 11");
}

static void test_closure(void) {
    printf("debugger: G closure state\n");
    const char *gee =
        "[ make-counter: func [n] [ func [delta] [ n: + n delta debug-break n ] ] "
        "  c: make-counter 5 "
        "  c 10 ]";
    const char *body =
        " st: continue "
        " ctx: state-get st 6 "
        " dbg-n: ctx-lookup ctx 'n "
        " continue ";
    expect_session1(gee, body, mk_int(15), "G: after continue, closure n == 15");
    CHECK(get_int("dbg-n") == mk_int(15), "G: captured environment live at breakpoint (n == 15)");
}

static void test_debugged_vs_normal(void) {
    printf("debugger: H debugged vs normal\n");
    /* same computation, one with a breakpoint (debugged), one without (normal) */
    const char *gee =
        "[ f: func [] [ x: 10 debug-break x: + x 5 x ]  f ]";
    const char *body =
        " st: continue  continue ";
    int N;
    debug_session(gee, body, &N);
    cell debugged = (N == 1) ? r0_s1_result(0, 1) : -1;

    int err = 0;
    cell nb = r0_s1_parse("[ f: func [] [ x: 10 x: + x 5 x ]  f ]", &err);
    int nN = r0_s1_run(nb);
    cell normal = (nN == 1) ? r0_s1_result(0, 1) : -1;

    CHECK(debugged == normal && normal == mk_int(15),
          "H: debugged (15) == normal (15) execution");
}

static void test_first_class_state(void) {
    printf("debugger: I first-class debug state\n");
    /* resume-it is an ordinary R0 function wrapping the RAW continue */
    const char *gee = "[ f: func [] [ x: 10 debug-break x: + x 5 x ]  f ]";
    const char *body =
        " resume-it: func [st] [ continue ] "
        " st: continue "
        " dbg-x: ctx-lookup state-get st 6 'x "
        " resume-it st ";
    expect_session1(gee, body, mk_int(15), "I: resume-it (ordinary R0 fn) resumes; result == 15");
    CHECK(get_int("dbg-x") == mk_int(10), "I: state passed at HLL level; x == 10");
}

static void test_stack_instrumentation(void) {
    printf("debugger: stack instrumentation (nested breakpoint)\n");
    const char *gee =
        "[ h: func [] [ debug-break 9 ]  g: func [] [ h ]  f: func [] [ g ]  f ]";
    const char *body =
        " st: continue "
        " dbg-sp: state-get-i st 1 "
        " dbg-rp: state-get-i st 2 "
        " continue ";
    int N;
    debug_session(gee, body, &N);
    cell result = (N == 1) ? r0_s1_result(0, 1) : -1;   /* capture before retrievals reset SP */
    long dbg_sp0 = (long)r0_s1_sp_start();   /* debugger data-stack baseline */
    long dbg_rp0 = (long)r0_s1_rp_start();   /* debugger return-stack baseline */
    long sp_end = (long)r0_s1_sp_end();      /* debuggee final SP */
    long rp_end = (long)r0_s1_rp_end();      /* debuggee final RP */
    long rp_min = (long)r0_s1_rp_min();      /* deepest RP seen */
    cell dbg_sp = get_int("dbg-sp");
    cell dbg_rp = get_int("dbg-rp");

    printf("    debugger baselines SP=%ld RP=%ld\n", dbg_sp0, dbg_rp0);
    printf("    debuggee baselines SP=%ld RP=%ld\n", (long)DBGEE_SP, (long)DBGEE_RP);
    printf("    SP/RP at breakpoint = %ld / %ld\n",
           (long)int_val(dbg_sp), (long)int_val(dbg_rp));
    printf("    deepest RP = %ld\n", rp_min);
    printf("    final SP=%ld RP=%ld (N=%d)\n", sp_end, rp_end, N);
    CHECK(rp_end == DBGEE_RP, "stack: debuggee final RP == its baseline (no leakage)");
    CHECK(N == 1 && result == mk_int(9), "stack: result == 9");
}

/* ================= acceptance: ordinary R0 closures during inspection ======
 * The HLL closure path (recursive + nested + lexical lookup) must work DURING
 * a suspended-debuggee inspection.  These are ordinary `func` definitions, not
 * the RAW ctx-lookup/frame-depth diagnostic workarounds. */
static void test_hll_inspection(void) {
    printf("debugger: HLL closure inspection (acceptance)\n");
    const char *gee = "[ f: func [a] [ b: + a 1 debug-break + b 10 ]  f 5 ]";
    const char *body =
        " count: func [n] [ either = n 0 [ 0 ] [ count - n 1 ] ] "
        " inc: func [n] [ + n 1 ] "
        " twice: func [g x] [ g g x ] "
        " find-in: func [ctx w i cnt] [ either >= i cnt [ lookup ctx-parent ctx w ] [ either = ctx-word ctx i w [ ctx-val ctx i ] [ find-in ctx w + i 1 cnt ] ] ] "
        " lookup: func [ctx w] [ either = ctx none [ -1 ] [ find-in ctx w 0 ctx-count ctx ] ] "
        " st: continue "
        " ctx: state-get st 6 "
        " dbg-a: lookup ctx 'a "
        " dbg-b: lookup ctx 'b "
        " dbg-rec: count 3 "
        " dbg-nested: twice :inc 5 "
        " continue ";
    expect_session1(gee, body, mk_int(16), "HLL: result == 16 after continue");
    CHECK(get_int("dbg-a") == mk_int(5), "HLL: lexical lookup a == 5");
    CHECK(get_int("dbg-b") == mk_int(6), "HLL: lexical lookup b == 6");
    CHECK(get_int("dbg-rec") == mk_int(0), "HLL: recursive count 3 == 0");
    CHECK(get_int("dbg-nested") == mk_int(7), "HLL: nested twice inc 5 == 7");
}

/* ================= stress: many suspend / inspect / resume cycles ========= */
static void test_stress(void) {
    printf("debugger: stress (100 suspend/inspect/resume cycles)\n");
    const char *gee = "[ f: func [] [ x: 10 debug-break x: + x 5 x ]  f ]";
    const char *body =
        " count: func [n] [ either = n 0 [ 0 ] [ count - n 1 ] ] "
        " st: continue "
        " dbg-r: count 10 "
        " continue ";
    int ok = 1;
    for (int i = 0; i < 100; i++) {
        int N;
        debug_session(gee, body, &N);
        if (N != 1 || r0_s1_result(0, 1) != mk_int(15)) { ok = 0; break; }
        if (r0_s1_rp_end() != DBGEE_RP) { ok = 0; break; }  /* RP back to debuggee baseline */
    }
    CHECK(ok, "stress: 100 cycles, correct result + clean RP every time");
}

/* scan a captured stderr file for unexpected machine diagnostics */
static void check_no_diagnostics(const char *path) {
    FILE *fp = fopen(path, "r");
    int bad = 0;
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        if (strstr(buf, "bad opcode")) { printf("  FAIL: unexpected 'bad opcode' diagnostic\n"); bad = 1; }
        if (strstr(buf, "[dump]"))     { printf("  FAIL: unexpected '[dump]' (unbound/error) diagnostic\n"); bad = 1; }
    }
    CHECK(bad == 0, "no unexpected bad-opcode / unbound-word diagnostics");
}

/* mechanical audit: no debugger semantics were added to the frozen runtime */
static void audit_runtime_clean(void) {
    static const char *forbidden[] = {
        "debug-break", "breakpoint", "suspend", "debugger", "resume",
        "DEBUG", "HOST_DEBUG", "HOST_BREAKPOINT", "HOST_CAPTURE", "HOST_RESUME"
    };
    int nbad = 10, violations = 0;
    FILE *fp = fopen("r0_s1_runtime.c", "r");
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        for (int i = 0; i < nbad; i++)
            if (strstr(buf, forbidden[i])) {
                printf("  FAIL: runtime mentions '%s'\n", forbidden[i]);
                violations++;
            }
    }
    fp = fopen("r0_s1.h", "r");
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        for (int i = 0; i < nbad; i++)
            if (strstr(buf, forbidden[i])) {
                printf("  FAIL: runtime header mentions '%s'\n", forbidden[i]);
                violations++;
            }
    }
    CHECK(violations == 0, "runtime/host contains no debugger semantics");
}

int run_r0_s1_debug_tests(void) {
    printf("R0-S1 D1 debugger: HLL-first cooperative debugger\n");

    /* capture stderr so we can prove ZERO unexpected machine diagnostics */
    const char *capture = "/tmp/opencode_debug_stderr.txt";
    int saved_err = dup(2);
    int capfd = open(capture, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }

    test_basic();
    test_locals();
    test_frames();
    test_unaware();
    test_side_effect();
    test_two_breakpoints();
    test_closure();
    test_debugged_vs_normal();
    test_stack_instrumentation();
    test_first_class_state();
    test_hll_inspection();
    test_stress();

    fflush(stderr);
    if (capfd >= 0) { dup2(saved_err, 2); close(saved_err); }
    check_no_diagnostics(capture);

    audit_runtime_clean();
    if (failures == 0) printf("all R0-S1 debugger tests passed\n");
    return failures;
}
