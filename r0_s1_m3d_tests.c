/* r0_s1_m3d_tests.c - M3D: managed runtime BLOCK! (immutable tagged-value series).
 *
 * Managed BLOCK! reuses T_BLOCK=6 over a managed GC_KIND_BLOCK=7 object with
 * payload [length, value0..valueN-1]. Elements are tagged R0 values, traced
 * generically by the collector. Blocks are immutable (functional append).
 *
 * GC: mark_value classifies T_BLOCK three ways (valid managed / permanent
 * loader / corrupt). Execution of a managed block is guarded at r_run_block /
 * r_values and fail-stops cleanly. See M3D-MANAGED-BLOCK-DESIGN.md.
 */

#include "r0_s1.h"
#include "r0_s1_m3c_lib.h"
#include "r0_s1_m3d_lib.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static cell alloc_addr, lookup_addr, collect_addr;
static char lib_buf[12288];
static char strlib_buf[4096];
static char blklib_buf[4096];
static char prog_buf[32768];

/* M3 datatype library (frozen glon-m3a-datatypes-v1 + string!/block! dispatch). */
static char *m3_lib(void) {
    snprintf(lib_buf, sizeof lib_buf,
        " untag: raw 1 [ DUP LIT 16 MOD SUB ARITY 1 EXIT ] "
        " type?: raw 1 [ "
        "   DUP LIT 16 MOD LIT T_USER EQ ZBRANCH Lbuiltin "
        "   LIT T_USER SUB @ ARITY 1 EXIT "
        " Lbuiltin: LIT 16 MOD LIT BUILTIN_BASE ADD @ ARITY 1 EXIT ] "
        " block-len: raw 1 [ DUP LIT 16 MOD SUB @ ARITY 1 EXIT ] "
        " block-pick: raw 2 [ "
        "   LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_B @ ADD @ ARITY 1 EXIT ] "
        " collect: raw [ CALL %ld ARITY 0 EXIT ] "
        " heap-high: raw [ LIT REG_HP @ ARITY 1 EXIT ] "
        " reject: raw [ ARITY 0 EXIT ] "
        " mk-datatype: raw 1 [ "
        "   DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ @ LIT 2 DIV LIT SCRATCH_C ! "
        "   LIT SCRATCH_C @ LIT 2 MUL LIT 2 ADD LIT 15 ADD LIT 16 DIV LIT 16 MUL "
        "   LIT GC_KIND_USER CALL %ld LIT SCRATCH_B ! "
        "   LIT GC_META @ LIT SCRATCH_B @ ! "
        "   LIT SCRATCH_C @ LIT 2 MUL LIT SCRATCH_B @ LIT 1 ADD ! "
        "   LIT 0 LIT SCRATCH_D ! "
        " Lmloop: LIT SCRATCH_D @ LIT SCRATCH_C @ GE ZBRANCH Lmfield "
        "   LIT SCRATCH_B @ LIT T_USER ADD ARITY 1 EXIT "
        " Lmfield: "
        "   LIT SCRATCH_D @ LIT 2 MUL LIT SCRATCH_A @ LIT 2 ADD ADD @ "
        "   DUP LIT 16 MOD SUB LIT 2 ADD "
        "   LIT SCRATCH_B @ LIT 2 ADD LIT SCRATCH_D @ LIT 2 MUL ADD ! "
        "   LIT SCRATCH_D @ LIT 2 MUL LIT SCRATCH_A @ LIT 3 ADD ADD @ "
        "   CALL %ld DUP LIT -1 EQ ZBRANCH Lmtype ARITY 0 EXIT "
        " Lmtype: LIT SCRATCH_B @ LIT 3 ADD LIT SCRATCH_D @ LIT 2 MUL ADD ! "
        "   LIT SCRATCH_D @ LIT 1 ADD LIT SCRATCH_D ! BRANCH Lmloop ] "
        " mk-value: raw 2 [ "
        "   LIT SCRATCH_B ! LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
        "   LIT SCRATCH_B @ DUP LIT 16 MOD SUB LIT SCRATCH_B ! "
        "   LIT SCRATCH_A @ LIT 1 ADD @ LIT 2 DIV LIT SCRATCH_C ! "
        "   LIT SCRATCH_B @ @ LIT SCRATCH_C @ NE ZBRANCH Lok ARITY 0 EXIT "
        " Lok: "
        "   LIT SCRATCH_C @ LIT 2 ADD LIT 15 ADD LIT 16 DIV LIT 16 MUL "
        "   LIT GC_KIND_USER CALL %ld LIT SCRATCH_E ! "
        "   LIT SCRATCH_A @ LIT T_USER ADD LIT SCRATCH_E @ ! "
        "   LIT SCRATCH_C @ LIT SCRATCH_E @ LIT 1 ADD ! "
        "   LIT 0 LIT SCRATCH_D ! "
        " Lvloop: LIT SCRATCH_D @ LIT SCRATCH_C @ GE ZBRANCH Lvfield "
        "   LIT SCRATCH_E @ LIT T_USER ADD ARITY 1 EXIT "
        " Lvfield: "
        "   LIT SCRATCH_D @ LIT SCRATCH_B @ LIT 2 ADD ADD @ "
        "   DUP LIT 16 MOD LIT 4 EQ ZBRANCH Lvplain "
        "   LIT 2 SUB CALL %ld DUP LIT -1 EQ ZBRANCH Lvresolved ARITY 0 EXIT "
        " Lvresolved: BRANCH Lvcheck "
        " Lvplain: "
        " Lvcheck: LIT SCRATCH_F ! "
        "   LIT SCRATCH_F @ DUP LIT 16 MOD LIT T_USER EQ ZBRANCH Lvbuiltin "
        "   LIT T_USER SUB @ BRANCH Lvgot "
        " Lvbuiltin: LIT 16 MOD LIT BUILTIN_BASE ADD @ "
        " Lvgot: LIT SCRATCH_D @ LIT 2 MUL LIT SCRATCH_A @ LIT 3 ADD ADD @ "
        "   EQ ZBRANCH Lvfail "
        "   LIT SCRATCH_F @ LIT SCRATCH_D @ LIT SCRATCH_E @ LIT 2 ADD ADD ! "
        "   LIT SCRATCH_D @ LIT 1 ADD LIT SCRATCH_D ! BRANCH Lvloop "
        " Lvfail: ARITY 0 EXIT ] "
        " field: raw 2 [ "
        "   LIT SCRATCH_E ! LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ @ DUP LIT 16 MOD SUB LIT SCRATCH_B ! "
        "   LIT SCRATCH_B @ LIT 1 ADD @ LIT 2 DIV LIT SCRATCH_C ! "
        "   LIT 0 LIT SCRATCH_D ! "
        " Lfloop: LIT SCRATCH_D @ LIT SCRATCH_C @ GE ZBRANCH Lfcmp ARITY 0 EXIT "
        " Lfcmp: LIT SCRATCH_D @ LIT 2 MUL LIT SCRATCH_B @ LIT 2 ADD ADD @ "
        "   LIT SCRATCH_E @ EQ ZBRANCH Lfnext "
        "   LIT SCRATCH_D @ LIT SCRATCH_A @ LIT 2 ADD ADD @ ARITY 1 EXIT "
        " Lfnext: LIT SCRATCH_D @ LIT 1 ADD LIT SCRATCH_D ! BRANCH Lfloop ] "
        " accepts?: func [t v] [ = type? v t ] "
        " datatype?: func [x] [ = type? x datatype! ] "
        " make: func [D args] [ "
        "   either = D datatype! [ mk-datatype args ] [ "
        "     either = D string! [ mk-string args ] [ "
        "       either = D block! [ mk-block args ] [ "
        "         either datatype? D [ mk-value D args ] [ reject ] ] ] ] ] ",
        (long)collect_addr, (long)alloc_addr, (long)lookup_addr, (long)alloc_addr,
        (long)lookup_addr);
    return lib_buf;
}

/* fresh non-multitasking M3D run */
static int m3d_run(const char *program, int *N) {
    int err = 0;
    r0_s1_init();
    r0_s1_seed_datatypes();
    alloc_addr = r0_s1_alloc_addr();
    lookup_addr = r0_s1_lookup_addr();
    collect_addr = r0_s1_gc_collect_addr();
    const char *sl = m3c_string_lib(strlib_buf, sizeof strlib_buf, alloc_addr);
    const char *bl = m3d_block_lib(blklib_buf, sizeof blklib_buf, alloc_addr);
    snprintf(prog_buf, sizeof prog_buf, "[ %s %s %s %s ]", m3_lib(), sl, bl, program);
    cell block = r0_s1_parse(prog_buf, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(block);
    return 0;
}

static void expect1(const char *program, cell want, const char *what) {
    int N; m3d_run(program, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == want);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)want); failures++; }
}

static void expect0(const char *program, const char *what) {
    int N; m3d_run(program, &N);
    if (N == 0) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d)\n", what, N); failures++; }
}

static int run_and_check_corruption(const char *tag, const char *program) {
    static char capture_path[64];
    snprintf(capture_path, sizeof capture_path, "/tmp/opencode_m3d_%s.txt", tag);
    int saved_err = dup(2);
    int capfd = open(capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }

    int N;
    m3d_run(program, &N);

    fflush(stderr);
    if (capfd >= 0) { dup2(saved_err, 2); close(saved_err); }

    FILE *fp = fopen(capture_path, "r");
    int bad = 0, dump = 0;
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        if (strstr(buf, "bad opcode")) bad = 1;
        if (strstr(buf, "[dump]")) dump = 1;
    }
    return (dump == 1 && bad == 0);
}

/* ============================== the tests =============================== */

static void test_A(void) {
    printf("m3d: A empty managed block construction\n");
    expect1(" b: make block! [] = length? b 0 ", mk_int(1),
            "A: make block! [] has length 0");
}

static void test_B(void) {
    printf("m3d: B append builds [1 2 3]\n");
    expect1(" b: make block! [] b: append b 1 b: append b 2 b: append b 3 "
            " = block-at b 0 1 ", mk_int(1), "B: block-at 0 == 1");
    expect1(" b: make block! [] b: append b 1 b: append b 2 b: append b 3 "
            " = block-at b 1 2 ", mk_int(1), "B: block-at 1 == 2");
    expect1(" b: make block! [] b: append b 1 b: append b 2 b: append b 3 "
            " = block-at b 2 3 ", mk_int(1), "B: block-at 2 == 3");
}

static void test_C(void) {
    printf("m3d: C original unchanged after append\n");
    expect1(" b: make block! [] c: append b 42 = length? b 0 ", mk_int(1),
            "C: original length unchanged");
    expect1(" b: make block! [7] c: append b 42 = block-at b 0 7 ", mk_int(1),
            "C: original element unchanged");
}

static void test_D(void) {
    printf("m3d: D length? correct after appends\n");
    expect1(" b: make block! [] b: append b 1 b: append b 2 = length? b 2 ", mk_int(1),
            "D: length 2 after 2 appends");
    expect1(" = length? make block! [1 2 3 4] 4 ", mk_int(1),
            "D: make block! [1 2 3 4] length 4");
}

static void test_E(void) {
    printf("m3d: E block-at out-of-range -> zero results\n");
    expect0(" b: make block! [] b: append b 1 block-at b 1 ", "E: index 1 of length-1 block");
    expect0(" b: make block! [] block-at b 0 ", "E: index 0 of empty block");
}

static void test_F(void) {
    printf("m3d: F STRING survives through BLOCK\n");
    expect1(" s: make string! [65 108 105 99 101] "
            " b: make block! [] b: append b :s "
            " collect "
            " = length? block-at b 0 5 ", mk_int(1),
            "F: string in block survives GC");
}

static void test_G(void) {
    printf("m3d: G T_USER survives through BLOCK\n");
    expect1(" person!: make datatype! [id: integer! name: string!] "
            " alice-name: make string! [65 108 105 99 101] "
            " alice: make person! [0 :alice-name] "
            " b: make block! [] b: append b :alice "
            " collect "
            " = field block-at b 0 'id 0 ", mk_int(1),
            "G: person in block survives GC");
}

static void test_H(void) {
    printf("m3d: H BLOCK survives through T_USER field\n");
    expect1(" person!: make datatype! [id: integer! name: string!] "
            " family!: make datatype! [people: block!] "
            " alice-name: make string! [65 108 105 99 101] "
            " alice: make person! [0 :alice-name] "
            " people: make block! [] people: append people :alice "
            " fam: make family! [:people] "
            " collect "
            " ps: field fam 'people "
            " = field block-at ps 0 'id 0 ", mk_int(1),
            "H: family -> block -> person -> string chain survives GC");
    expect1(" person!: make datatype! [id: integer! name: string!] "
            " family!: make datatype! [people: block!] "
            " alice-name: make string! [65 108 105 99 101] "
            " alice: make person! [0 :alice-name] "
            " people: make block! [] people: append people :alice "
            " fam: make family! [:people] "
            " collect "
            " ps: field fam 'people "
            " = length? field block-at ps 0 'name 5 ", mk_int(1),
            "H: nested string bytes intact through the chain");
}

static void test_I(void) {
    printf("m3d: I nested managed BLOCK survives GC\n");
    expect1(" inner: make block! [] inner: append inner 42 "
            " outer: make block! [] outer: append outer :inner "
            " collect "
            " = block-at block-at outer 0 0 42 ", mk_int(1),
            "I: nested managed block survives GC");
}

static void test_J(void) {
    printf("m3d: J unreachable managed BLOCK reclaimed\n");
    int N;
    m3d_run(" b: make block! [] b: append b 1 b: none collect ", &N);
    CHECK(r0_s1_gc_free_cells() >= 32, "J: unreachable managed block reclaimed (>= 32 cells)");
}

static void test_K(void) {
    printf("m3d: K corrupt length fail-stops\n");
    char prog[2048];
    snprintf(prog, sizeof prog,
        " mk-badblk: raw 1 [ LIT SCRATCH_A ! LIT 16 LIT GC_KIND_BLOCK CALL %ld LIT SCRATCH_B ! "
        " LIT 100 LIT SCRATCH_B @ ! LIT SCRATCH_B @ LIT T_BLOCK ADD ARITY 1 EXIT ] "
        " b: mk-badblk none collect ", (long)alloc_addr);
    CHECK(run_and_check_corruption("badblk", prog),
          "K: corrupt block length halts cleanly as corruption");
}

static void test_L(void) {
    printf("m3d: L T_BLOCK three-way classification\n");
    CHECK(run_and_check_corruption("forge",
            " forge: raw [ LIT 16 LIT T_BLOCK ADD ARITY 1 EXIT ] x: forge collect "),
          "L: out-of-range T_BLOCK (below heap) fail-stops");
    CHECK(run_and_check_corruption("wrongkind",
            " mk-badblk-kind: raw 1 [ DUP LIT 16 MOD SUB LIT T_BLOCK ADD ARITY 1 EXIT ] "
            " s: make string! [65] x: mk-badblk-kind s collect "),
          "L: T_BLOCK pointing at a STRING payload (wrong kind) fail-stops");
    CHECK(run_and_check_corruption("unalloc",
            " mk-badblk-unalloc: raw [ LIT REG_HP @ LIT T_BLOCK ADD ARITY 1 EXIT ] "
            " x: mk-badblk-unalloc collect "),
          "L: T_BLOCK pointing at unallocated heap (REG_HP) fail-stops");
}

static void test_M(void) {
    printf("m3d: M permanent loader block behaviour unchanged\n");
    expect1(" = tag-int block-len [1 2 3] 3 ", mk_int(1),
            "M: loader block-len unchanged");
    expect1(" = block-pick [10 20 30] div16 1 20 ", mk_int(1),
            "M: loader block-pick unchanged");
    expect1(" do [1 2 3] ", mk_int(3),
            "M: loader block still evaluates as code");
}

static void test_N(void) {
    printf("m3d: N managed block execution guard\n");
    CHECK(run_and_check_corruption("do-blk",
            " b: make block! [] do b "),
          "N: do on a managed block fail-stops cleanly");
    CHECK(run_and_check_corruption("values-blk",
            " b: make block! [1 2 3] values b "),
          "N: values on a managed block fail-stops cleanly");
    CHECK(run_and_check_corruption("either-blk",
            " b: make block! [1] either 1 b b "),
          "N: either with a managed branch fail-stops cleanly");
}

int run_r0_s1_m3d_tests(void) {
    printf("R0-S1 M3D: managed runtime BLOCK! above frozen S1\n");

    test_A();
    test_B();
    test_C();
    test_D();
    test_E();
    test_F();
    test_G();
    test_H();
    test_I();
    test_J();
    test_K();
    test_L();
    test_M();
    test_N();

    if (failures == 0) printf("all R0-S1 M3D managed-block tests passed\n");
    return failures;
}
