/* r0_s1_m3_tests.c - M3: extensible structured datatypes above the frozen R0/S1.
 *
 * A single generic user-object tag (T_USER=11), one generic GC kind
 * (GC_KIND_USER), a generic trace_user, a fixed 16-slot BUILTIN_TYPE table, and
 * a small datatype library (generic RAW mechanics + ordinary GLON policy) give
 * user-defined structured datatypes with field-type checking. See
 * M3-DATATYPE-DESIGN.md.
 */

#include "r0_s1.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static cell alloc_addr, lookup_addr, collect_addr;
static char lib_buf[12288];
static char prog_buf[32768];

/* M3 datatype library: generic RAW mechanics + ordinary GLON policy.
 *
 * User value layout:   [desc(tagged), count(raw), field0..fieldN-1 (tagged)]
 * Descriptor layout:   [datatype!(tagged), 2N(raw), name0,type0,...,name(N-1),type(N-1)]
 * Field i name at descriptor[2+2i], type at descriptor[3+2i]; value field i at
 * value[2+i]. */
static char *m3_lib(void) {
    snprintf(lib_buf, sizeof lib_buf,
        /* --- generic RAW mechanics --- */
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
        /* build a datatype descriptor from [set-word type-word ...] */
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
        /* build a value of type D from a block of field values (get-words are
         * resolved via lookup). Validates each field's type; success = 1 result,
         * rejection = 0 results. */
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
        /* field read: (value name-word -> field | 0 results) */
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
        /* field write: (value name-word new -> value | 0 results), type-checked */
        " field-set!: raw 3 [ "
        "   LIT SCRATCH_F ! LIT SCRATCH_E ! LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
        "   LIT SCRATCH_A @ @ DUP LIT 16 MOD SUB LIT SCRATCH_B ! "
        "   LIT SCRATCH_B @ LIT 1 ADD @ LIT 2 DIV LIT SCRATCH_C ! "
        "   LIT 0 LIT SCRATCH_D ! "
        " Lsloop: LIT SCRATCH_D @ LIT SCRATCH_C @ GE ZBRANCH Lscmp ARITY 0 EXIT "
        " Lscmp: LIT SCRATCH_D @ LIT 2 MUL LIT SCRATCH_B @ LIT 2 ADD ADD @ "
        "   LIT SCRATCH_E @ EQ ZBRANCH Lsnext "
        "   LIT SCRATCH_D @ LIT 2 MUL LIT SCRATCH_B @ LIT 3 ADD ADD @ LIT SCRATCH_E ! "
        "   LIT SCRATCH_F @ DUP LIT 16 MOD LIT T_USER EQ ZBRANCH Lsbuiltin "
        "   LIT T_USER SUB @ BRANCH Lsgot "
        " Lsbuiltin: LIT 16 MOD LIT BUILTIN_BASE ADD @ "
        " Lsgot: LIT SCRATCH_E @ EQ ZBRANCH Lsfail "
        "   LIT SCRATCH_F @ LIT SCRATCH_D @ LIT SCRATCH_A @ LIT 2 ADD ADD ! "
        "   LIT SCRATCH_A @ LIT T_USER ADD ARITY 1 EXIT "
        " Lsfail: ARITY 0 EXIT "
        " Lsnext: LIT SCRATCH_D @ LIT 1 ADD LIT SCRATCH_D ! BRANCH Lsloop ] "
        /* test-only: build a one-field self-referential value (cycles) */
        " mk-cycle: raw 1 [ "
        "   LIT SCRATCH_A ! "
        "   LIT 16 LIT GC_KIND_USER CALL %ld LIT SCRATCH_B ! "
        "   LIT SCRATCH_A @ LIT SCRATCH_B @ ! "
        "   LIT 1 LIT SCRATCH_B @ LIT 1 ADD ! "
        "   LIT SCRATCH_B @ LIT T_USER ADD LIT SCRATCH_B @ LIT 2 ADD ! "
        "   LIT SCRATCH_B @ LIT T_USER ADD ARITY 1 EXIT ] "
        /* test-only: build a user object whose COUNT exceeds its payload (corruption) */
        " mk-badcount: raw 1 [ "
        "   LIT SCRATCH_A ! "
        "   LIT 16 LIT GC_KIND_USER CALL %ld LIT SCRATCH_B ! "
        "   LIT SCRATCH_A @ LIT SCRATCH_B @ ! "
        "   LIT 100 LIT SCRATCH_B @ LIT 1 ADD ! "
        "   LIT SCRATCH_B @ LIT T_USER ADD ARITY 1 EXIT ] "
        /* --- GLON policy --- */
        " accepts?: func [t v] [ = type? v t ] "
        " datatype?: func [x] [ = type? x datatype! ] "
        " make: func [D args] [ "
        "   either = D datatype! [ mk-datatype args ] [ "
        "     either datatype? D [ mk-value D args ] [ reject ] ] ] ",
        (long)collect_addr, (long)alloc_addr, (long)lookup_addr, (long)alloc_addr,
        (long)lookup_addr, (long)alloc_addr, (long)alloc_addr);
    return lib_buf;
}

/* fresh non-multitasking M3 run */
static int m3_run(const char *program, int *N) {
    int err = 0;
    r0_s1_init();
    r0_s1_seed_datatypes();
    alloc_addr = r0_s1_alloc_addr();
    lookup_addr = r0_s1_lookup_addr();
    collect_addr = r0_s1_gc_collect_addr();
    snprintf(prog_buf, sizeof prog_buf, "[ %s %s ]", m3_lib(), program);
    cell block = r0_s1_parse(prog_buf, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(block);
    return 0;
}

static void expect1(const char *program, cell want, const char *what) {
    int N; m3_run(program, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == want);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)want); failures++; }
}

static void expect0(const char *program, const char *what) {
    int N; m3_run(program, &N);
    if (N == 0) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d)\n", what, N); failures++; }
}

/* ============================== the tests =============================== */

static void test_vector(void) {
    printf("m3: A vector create/construct/type-check\n");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " v: make vector! [1 2 3] = type? v vector! ", mk_int(1),
            "A: make vector! [1 2 3] has type vector!");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " vector?: func [v] [= type? v vector!] "
            " v: make vector! [1 2 3] vector? v ", mk_int(1),
            "A: vector? predicate true");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " vector?: func [v] [= type? v vector!] "
            " v: make vector! [1 2 3] vector? 42 ", mk_int(0),
            "A: vector? 42 is false");
}

static void test_fields(void) {
    printf("m3: B field access\n");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " v: make vector! [1 2 3] field v 'x ", mk_int(1), "B: field x == 1");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " v: make vector! [1 2 3] field v 'y ", mk_int(2), "B: field y == 2");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " v: make vector! [1 2 3] field v 'z ", mk_int(3), "B: field z == 3");
}

static void test_first_class(void) {
    printf("m3: C first-class value\n");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " v: make vector! [1 2 3] id: func [w] [ w ] "
            " field id v 'y ", mk_int(2), "C: value passes through unaware fn");
}

static void test_nesting(void) {
    printf("m3: D nested person/relationship + GC\n");
    expect1(" person!: make datatype! [id: integer! name: word!] "
            " relationship!: make datatype! [kind: word! from: person! to: person!] "
            " p1: make person! [1 alice] p2: make person! [2 bob] "
            " r: make relationship! [friend :p1 :p2] collect "
            " field field r 'from 'id ", mk_int(1),
            "D: nested relationship.from.id survives GC");
    expect1(" person!: make datatype! [id: integer! name: word!] "
            " relationship!: make datatype! [kind: word! from: person! to: person!] "
            " p1: make person! [1 alice] p2: make person! [2 bob] "
            " r: make relationship! [friend :p1 :p2] collect "
            " field field r 'to 'id ", mk_int(2),
            "D: nested relationship.to.id survives GC");
}

static void test_cycles(void) {
    printf("m3: E cyclic user value survives GC\n");
    expect1(" person!: make datatype! [friend: word!] "
            " a: mk-cycle person! = field a 'friend a ", mk_int(1),
            "E: self-referential (no collect)");
    expect1(" person!: make datatype! [friend: word!] "
            " a: mk-cycle person! collect = field a 'friend a ", mk_int(1),
            "E: self-referential user value survives GC");
}

static void test_reclaim(void) {
    printf("m3: F unreachable value reclaimed\n");
    int N;
    m3_run(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
           " v: make vector! [1 2 3] v: none collect ", &N);
    CHECK(r0_s1_gc_free_cells() >= 32, "F: unreachable value reclaimed (>= 32 cells)");

    printf("m3: G unreachable nested graph reclaimed\n");
    m3_run(" person!: make datatype! [id: integer! name: word!] "
           " relationship!: make datatype! [kind: word! from: person! to: person!] "
           " p1: make person! [1 alice] p2: make person! [2 bob] "
           " r: make relationship! [friend :p1 :p2] "
           " r: none p1: none p2: none collect ", &N);
    CHECK(r0_s1_gc_free_cells() >= 96, "G: unreachable persons+relationship reclaimed");
}

static void test_descriptor_lifetime(void) {
    printf("m3: H descriptor lifetime follows value\n");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " v: make vector! [1 2 3] vector!: none collect field v 'x ", mk_int(1),
            "H: descriptor survives collect while its value is reachable");
}

static void test_closure_capture(void) {
    printf("m3: I user value captured in closure survives GC\n");
    expect1(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " mk: func [] [ v: make vector! [1 2 3] func [] [ v ] ] "
            " c: mk collect field c 'z ",
            mk_int(3), "I: closure-captured value survives GC");
}

static void test_datatype_loop(void) {
    printf("m3: J datatype? closed loop\n");
    expect1(" vector!: make datatype! [x: integer!] "
            " = datatype? vector! datatype? datatype! ", mk_int(1),
            "J: datatype? vector! and datatype? datatype! both true");
    expect1(" vector!: make datatype! [x: integer!] "
            " v: make vector! [7] datatype? v ", mk_int(0),
            "J: datatype? v (a value) is false");
}

static void test_immediates_not_roots(void) {
    printf("m3: K immediates are not roots\n");
    int N;
    m3_run(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
           " v: make vector! [1 2 3] v: none big: 2048 collect ", &N);
    CHECK(r0_s1_gc_free_cells() >= 32,
          "K: heap-range integer did not pin the dead value");
}

static void test_validation(void) {
    printf("m3: L construction/mutation validation\n");
    expect0(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " make vector! [1 [2] 3] ", "L: make rejects wrong field type (0 results)");
    expect0(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " make vector! [1 2] ", "L: make rejects arity mismatch");
    expect0(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " v: make vector! [1 2 3] make v [1 2 3] ", "L: make rejects a non-datatype");
    expect0(" vector!: make datatype! [x: integer! y: integer! z: integer!] "
            " v: make vector! [1 2 3] field-set! v 'y [2] ", "L: field-set! rejects wrong type");
}

static void test_accepts(void) {
    printf("m3: M accepts? built-in vs user unified\n");
    expect1(" = accepts? integer! 42 accepts? word! 42 ", mk_int(0),
            "M: accepts? integer! 42 true, accepts? word! 42 false");
    expect1(" vector!: make datatype! [x: integer!] "
            " v: make vector! [7] = accepts? vector! v accepts? vector! 42 ", mk_int(0),
            "M: accepts? vector! v true, accepts? vector! 42 false");
}

static void test_bootstrap_root(void) {
    printf("m3: N bootstrap root independent of bindings\n");
    expect1(" integer!: none collect = type? 42 integer! ", mk_int(0),
            "N: after rebinding integer!, type? 42 != integer! (word)");
    expect1(" integer!: none collect type? 42 ", mk_user(BUILTIN0_PAYLOAD),
            "N: type? 42 still yields the canonical integer descriptor");
}

static void test_oom(void) {
    printf("m3: O clean out-of-memory\n");
    const char *capture = "/tmp/opencode_m3_oom.txt";
    int saved_err = dup(2);
    int capfd = open(capture, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }

    int N;
    m3_run(" fact: func [n] [ either <= n 1 [ 1 ] [ * n fact - n 1 ] ] fact 400 ", &N);

    fflush(stderr);
    if (capfd >= 0) { dup2(saved_err, 2); close(saved_err); }

    FILE *fp = fopen(capture, "r");
    int bad = 0;
    if (fp) {
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        if (strstr(buf, "bad opcode")) bad = 1;
    }
    CHECK(bad == 0, "O: OOM halts cleanly (no bad opcode / corruption)");
}

/* capture stderr around a run; returns 1 if the collector reported corruption
 * via a clean HOST_DUMP halt (not a raw machine "bad opcode"). */
static int run_and_check_corruption(const char *tag, const char *program) {
    static char capture_path[64];
    snprintf(capture_path, sizeof capture_path, "/tmp/opencode_m3_%s.txt", tag);
    int saved_err = dup(2);
    int capfd = open(capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 2); close(capfd); }

    int N;
    m3_run(program, &N);

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

static void test_forged_user(void) {
    printf("m3: P forged/out-of-range T_USER is corruption\n");
    CHECK(run_and_check_corruption("forge",
            " forge: raw [ LIT 16 LIT T_USER ADD ARITY 1 EXIT ] x: forge collect "),
          "P: forged out-of-range T_USER halts cleanly as corruption");
}

static void test_bad_count(void) {
    printf("m3: Q USER count beyond extent is corruption\n");
    CHECK(run_and_check_corruption("badcount",
            " vector!: make datatype! [x: integer!] x: mk-badcount vector! collect "),
          "Q: USER count beyond allocated extent halts cleanly as corruption");
}

int run_r0_s1_m3_tests(void) {
    printf("R0-S1 M3: extensible structured datatypes above frozen S1\n");

    test_vector();
    test_fields();
    test_first_class();
    test_nesting();
    test_cycles();
    test_reclaim();
    test_descriptor_lifetime();
    test_closure_capture();
    test_datatype_loop();
    test_immediates_not_roots();
    test_validation();
    test_accepts();
    test_bootstrap_root();
    test_oom();
    test_forged_user();
    test_bad_count();

    if (failures == 0) printf("all R0-S1 M3 datatype tests passed\n");
    return failures;
}
