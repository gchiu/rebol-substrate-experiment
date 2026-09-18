/* r0_s1_m3c_tests.c - M3C: managed STRING! (immutable UTF-8 byte series).
 *
 * STRING! is the first intrinsic physical representation above the frozen
 * R0/S1 substrate: tag T_STRING=12 over a managed, immutable byte series
 * (GC_KIND_STRING=6), payload [length, byte0..byteN-1] with one raw byte
 * (0..255) per cell. See M3C-STRING-SERIES-DESIGN.md.
 *
 * The library is generic RAW mechanics + ordinary GLON policy (see
 * r0_s1_m3c_lib.h). No new S1 primitive, no STRING-specific HOST service, no
 * evaluator/parser special case, no new native id.
 */

#include "r0_s1.h"
#include "r0_s1_m3c_lib.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static cell alloc_addr, lookup_addr, collect_addr;
static char lib_buf[12288];
static char strlib_buf[4096];
static char prog_buf[32768];

/* M3 datatype library: generic RAW mechanics + ordinary GLON policy
 * (identical to the frozen glon-m3a-datatypes-v1 library). */
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
        "       either datatype? D [ mk-value D args ] [ reject ] ] ] ] ",
        (long)collect_addr, (long)alloc_addr, (long)lookup_addr, (long)alloc_addr,
        (long)lookup_addr);
    return lib_buf;
}

/* fresh non-multitasking M3C run */
static int m3c_run(const char *program, int *N) {
    int err = 0;
    r0_s1_init();
    r0_s1_seed_datatypes();
    alloc_addr = r0_s1_alloc_addr();
    lookup_addr = r0_s1_lookup_addr();
    collect_addr = r0_s1_gc_collect_addr();
    const char *sl = m3c_string_lib(strlib_buf, sizeof strlib_buf, alloc_addr);
    snprintf(prog_buf, sizeof prog_buf, "[ %s %s %s ]", m3_lib(), sl, program);
    cell block = r0_s1_parse(prog_buf, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(block);
    return 0;
}

static void expect1(const char *program, cell want, const char *what) {
    int N; m3c_run(program, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == want);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)want); failures++; }
}

static void expect0(const char *program, const char *what) {
    int N; m3c_run(program, &N);
    if (N == 0) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d)\n", what, N); failures++; }
}

/* capture stderr around a run; returns 1 iff the collector halted cleanly via
 * HOST_DUMP (not a raw machine "bad opcode"). */
static int run_and_check_corruption(const char *tag, const char *program) {
    static char capture_path[64];
    snprintf(capture_path, sizeof capture_path, "/tmp/opencode_m3c_%s.txt", tag);
    int saved_err = dup(2);
    int capfd = open(capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }

    int N;
    m3c_run(program, &N);

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
    printf("m3c: A make string! constructs Alice\n");
    expect1(" s: make string! [65 108 105 99 101] = length? s 5 ", mk_int(1),
            "A: make string! [65 108 105 99 101] has length 5");
}

static void test_B(void) {
    printf("m3c: B type? returns string!\n");
    expect1(" s: make string! [65 108 105 99 101] = type? s string! ", mk_int(1),
            "B: type? s == string!");
    expect1(" s: make string! [65 108 105 99 101] string? s ", mk_int(1),
            "B: string? predicate true");
    expect1(" string? 42 ", mk_int(0), "B: string? 42 is false");
}

static void test_C(void) {
    printf("m3c: C length? returns byte length\n");
    expect1(" = length? make string! [65 108 105 99 101] 5 ", mk_int(1),
            "C: length? == 5 (bytes)");
    expect1(" = length? make string! [] 0 ", mk_int(1),
            "C: empty string length 0");
}

static void test_D(void) {
    printf("m3c: D string-byte returns correct bytes\n");
    expect1(" s: make string! [65 108 105 99 101] = string-byte s 0 65 ", mk_int(1),
            "D: byte 0 == 65");
    expect1(" s: make string! [65 108 105 99 101] = string-byte s 1 108 ", mk_int(1),
            "D: byte 1 == 108");
    expect1(" s: make string! [65 108 105 99 101] = string-byte s 4 101 ", mk_int(1),
            "D: byte 4 == 101");
}

static void test_E(void) {
    printf("m3c: E append returns a new string, original unchanged\n");
    expect1(" s: make string! [65 108] t: append s 105 = length? s 2 ", mk_int(1),
            "E: original length unchanged");
    expect1(" s: make string! [65 108] t: append s 105 = length? t 3 ", mk_int(1),
            "E: new string length +1");
    expect1(" s: make string! [65 108] t: append s 105 = string-byte t 2 105 ", mk_int(1),
            "E: appended byte at end");
    expect1(" s: make string! [65 108] t: append s 105 = string-byte t 0 65 ", mk_int(1),
            "E: old bytes copied");
    expect1(" s: make string! [65 108] t: append s 105 = string-byte s 1 108 ", mk_int(1),
            "E: original bytes intact");
}

static void test_F(void) {
    printf("m3c: F string=? compares content, not identity\n");
    expect1(" a: make string! [65 108] b: make string! [65 108] string=? a b ", mk_int(1),
            "F: equal content -> true");
    expect1(" a: make string! [65 108] b: make string! [65 108] = a b ", mk_int(0),
            "F: distinct objects are not = (identity)");
    expect1(" a: make string! [65 108] b: make string! [66 111] string=? a b ", mk_int(0),
            "F: different content -> false");
}

static void test_G(void) {
    printf("m3c: G copy is semantically equal; identity return permitted\n");
    expect1(" s: make string! [65 108] c: copy s string=? s c ", mk_int(1),
            "G: copy is content-equal");
    expect1(" s: make string! [65 108] c: copy s = s c ", mk_int(1),
            "G: copy returns s itself (immutable)");
}

static void test_H(void) {
    printf("m3c: H invalid byte (<0 or >255) rejected\n");
    expect0(" make string! [65 -1] ", "H: byte -1 rejected");
    expect0(" make string! [65 256] ", "H: byte 256 rejected");
}

static void test_I(void) {
    printf("m3c: I wrong input datatype rejected\n");
    expect0(" make string! [65 [108]] ", "I: block element rejected");
}

static void test_J(void) {
    printf("m3c: J reachable STRING survives GC\n");
    expect1(" s: make string! [65 108 105 99 101] collect = length? s 5 ", mk_int(1),
            "J: reachable string survives collect");
}

static void test_K(void) {
    printf("m3c: K unreachable STRING reclaimed\n");
    int N;
    m3c_run(" s: make string! [65 108 105 99 101] s: none collect ", &N);
    CHECK(r0_s1_gc_free_cells() >= 32, "K: unreachable string reclaimed (>= 32 cells)");
}

static void test_L(void) {
    printf("m3c: L out-of-range T_STRING is corruption\n");
    CHECK(run_and_check_corruption("forge",
            " forge: raw [ LIT 16 LIT T_STRING ADD ARITY 1 EXIT ] x: forge collect "),
          "L: forged out-of-range T_STRING halts cleanly as corruption");
}

static void test_M(void) {
    printf("m3c: M corrupt length exceeding extent is corruption\n");
    char prog[2048];
    snprintf(prog, sizeof prog,
        " mk-badlen: raw 1 [ LIT SCRATCH_A ! LIT 16 LIT GC_KIND_STRING CALL %ld LIT SCRATCH_B ! "
        " LIT 100 LIT SCRATCH_B @ ! LIT SCRATCH_B @ LIT T_STRING ADD ARITY 1 EXIT ] "
        " s: mk-badlen none collect ", (long)alloc_addr);
    CHECK(run_and_check_corruption("badlen", prog),
          "M: corrupt string length halts cleanly as corruption");
}

static void test_N(void) {
    printf("m3c: N STRING nested in an M3 user datatype survives GC\n");
    expect1(" person!: make datatype! [id: integer! name: string!] "
            " alice-name: make string! [65 108 105 99 101] "
            " p: make person! [0 :alice-name] "
            " collect "
            " = length? field p 'name 5 ", mk_int(1),
            "N: string nested in person survives GC");
    expect1(" person!: make datatype! [id: integer! name: string!] "
            " alice-name: make string! [65 108 105 99 101] "
            " p: make person! [0 :alice-name] "
            " collect "
            " = string-byte field p 'name 0 65 ", mk_int(1),
            "N: nested string bytes intact after GC");
}

/* Mechanical audit: STRING! (as a concrete intrinsic) must not appear in the
 * frozen substrate/evaluator/parser/HOST beyond the tag/kind/bootstrap
 * constants in r0_s1.h and the generic trace path in r0_s1_runtime.c. */
static int file_contains(const char *path, const char *needle) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    static char buf[1 << 20];
    size_t nb = fread(buf, 1, sizeof buf - 1, fp);
    buf[nb] = 0; fclose(fp);
    for (size_t i = 0; i < nb; i++) buf[i] = (char)tolower((unsigned char)buf[i]);
    return strstr(buf, needle) != NULL;
}

static void test_S(void) {
    printf("m3c: S STRING! absent from frozen substrate/evaluator/parser/HOST\n");
    /* "string" appears only as T_STRING/GC_KIND_STRING constants + the generic
     * mark_value/trace_string/drain/bootstrap sites; it must NOT appear in the
     * frozen S1 files or the parser/evaluator emitters. */
    int clean = 1;
    if (file_contains("s1.c", "string")) { printf("  FAIL: string in s1.c\n"); clean = 0; }
    if (file_contains("s1.h", "string")) { printf("  FAIL: string in s1.h\n"); clean = 0; }
    if (file_contains("tests.c", "string")) { printf("  FAIL: string in tests.c\n"); clean = 0; }
    if (file_contains("adversarial.c", "string")) { printf("  FAIL: string in adversarial.c\n"); clean = 0; }
    if (file_contains("claims.c", "string")) { printf("  FAIL: string in claims.c\n"); clean = 0; }
    CHECK(clean, "S: no STRING! semantics in frozen substrate/evaluator/parser/HOST");
}

int run_r0_s1_m3c_tests(void) {
    printf("R0-S1 M3C: managed immutable STRING! above frozen S1\n");

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
    test_S();

    if (failures == 0) printf("all R0-S1 M3C string tests passed\n");
    return failures;
}
