/* r0_s1_m3b_tests.c - M3B: a genealogy graph rendered as a generic surface
 * command stream, above the frozen R0/S1 architecture.
 *
 * M3B reuses the frozen M3A datatype mechanism unchanged to define five
 * ordinary datatypes (vector!, person!, relationship!, graph-node!,
 * graph-edge!), builds a tiny fixed family (Alice/Bob/Charlie), and renders it
 * as a stream of integers over the frozen HOST_PRINT ("print") path.
 *
 * The GLON side emits a *generic* surface-command stream (clear/line/text/
 * rect/present with integer arguments). A small host-side decoder (in this
 * test file, demonstration tooling only) converts that stream into an SVG
 * file. The decoder knows only generic drawing operations; it knows nothing
 * about vector!/person!/relationship!/graph-node!/graph-edge! or genealogy
 * semantics.
 *
 * Display labels are managed STRING! values (M3C): a string is a byte series
 * whose bytes carry UTF-8 text. WORD spellings live in the C-side interner and
 * are not visible to GLON, so symbol ids are never rendered.
 *
 * GC acceptance tests A-G exercise the planned lifetime/composition cases; H
 * (M3A A-Q and earlier suites unchanged) is verified by the full native suite.
 * See M3B-GENEALOGY-GRAPH-DESIGN.md and M3C-STRING-SERIES-DESIGN.md.
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

/* M3 datatype library: generic RAW mechanics + ordinary GLON policy.
 * Identical to the frozen glon-m3a-datatypes-v1 library (see r0_s1_m3_tests.c).
 * User value layout:   [desc(tagged), count(raw), field0..fieldN-1 (tagged)]
 * Descriptor layout:   [datatype!(tagged), 2N(raw), name0,type0,...,name(N-1),type(N-1)] */
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

/* M3B datatype definitions: totally ordered (each field type is already
 * defined above it). `name` is a managed STRING! (M3C), so a display label is
 * GLON-visible presentation text. */
#define M3B_TYPES \
    " vector!: make datatype! [x: integer! y: integer!] " \
    " person!: make datatype! [id: integer! name: string!] " \
    " relationship!: make datatype! [kind: word! from: person! to: person!] " \
    " graph-node!: make datatype! [person: person! position: vector!] " \
    " graph-edge!: make datatype! [relationship: relationship! from-node: graph-node! to-node: graph-node!] "

/* M3B rendering library: generic surface-command emitters over the frozen
 * `print` path. These know about the surface protocol only (opcodes 0..4 with
 * integer arguments); they read coordinates from graph values and never
 * hardcode genealogy. Text bytes come from STRING! via length?/string-byte.
 * The coordinate helpers (node-x/node-y/node-name) are ordinary closures
 * invoked inside other closures' argument lists, which exercises the corrected
 * nested-closure argument evaluation (see r0_s1_nested_closure_tests.c). */
#define M3B_RENDER \
    " node-x: func [n] [ field field n 'position 'x ] " \
    " node-y: func [n] [ field field n 'position 'y ] " \
    " node-name: func [n] [ field field n 'person 'name ] " \
    " emit-chars: func [b i n] [ either < i n [ print string-byte b i emit-chars b + i 1 n ] [ none ] ] " \
    " draw-clear: func [] [ print 0 ] " \
    " draw-line: func [x1 y1 x2 y2] [ print 1 print x1 print y1 print x2 print y2 ] " \
    " draw-text: func [x y name] [ print 2 print x print y print length? name emit-chars name 0 length? name ] " \
    " draw-rect: func [x y w h] [ print 3 print x print y print w print h ] " \
    " draw-present: func [] [ print 4 ] " \
    " draw-node: func [n] [ " \
    "   draw-rect node-x n node-y n 60 30 " \
    "   draw-text + node-x n 8 + node-y n 21 node-name n ] " \
    " draw-edge: func [e] [ " \
    "   draw-line + node-x field e 'from-node 30 + node-y field e 'from-node 15 " \
    "             + node-x field e 'to-node 30 + node-y field e 'to-node 15 ] "

/* The fixed family dataset: Alice (id 0), Bob (id 1), Charlie (id 2), with
 * spouse(Alice,Bob), parent(Alice,Charlie), parent(Bob,Charlie) and explicit
 * VECTOR positions. Names are STRING! values built from byte codes. */
#define M3B_GRAPH \
    " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] " \
    " bob-name: make string! [66 111 98] bob: make person! [1 :bob-name] " \
    " charlie-name: make string! [67 104 97 114 108 105 101] charlie: make person! [2 :charlie-name] " \
    " spouse: make relationship! [spouse :alice :bob] " \
    " parent1: make relationship! [parent :alice :charlie] " \
    " parent2: make relationship! [parent :bob :charlie] " \
    " pos-a: make vector! [100 100] " \
    " pos-b: make vector! [300 100] " \
    " pos-c: make vector! [200 250] " \
    " node-a: make graph-node! [:alice :pos-a] " \
    " node-b: make graph-node! [:bob :pos-b] " \
    " node-c: make graph-node! [:charlie :pos-c] " \
    " edge-spouse: make graph-edge! [:spouse :node-a :node-b] " \
    " edge-parent1: make graph-edge! [:parent1 :node-a :node-c] " \
    " edge-parent2: make graph-edge! [:parent2 :node-b :node-c] "

#define M3B_RENDER_CALL \
    " render: func [] [ draw-clear draw-edge edge-spouse draw-edge edge-parent1 draw-edge edge-parent2 draw-node node-a draw-node node-b draw-node node-c draw-present ] " \
    " render "

/* fresh non-multitasking M3B run */
static int m3b_run(const char *program, int *N) {
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
    int N; m3b_run(program, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == want);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)want); failures++; }
}

/* ============================== the tests =============================== */

static void test_A(void) {
    printf("m3b: A VECTOR nested in GRAPH-NODE survives GC\n");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " pos-a: make vector! [100 100] "
            " node-a: make graph-node! [:alice :pos-a] "
            " collect "
            " field field node-a 'position 'x ", mk_int(100),
            "A: node-a.position.x == 100 after GC");
}

static void test_B(void) {
    printf("m3b: B PERSON reachable only through GRAPH-NODE survives GC\n");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " pos-a: make vector! [100 100] "
            " node-a: make graph-node! [:alice :pos-a] "
            " alice: none pos-a: none collect "
            " field field node-a 'person 'id ", mk_int(0),
            "B: node-a.person.id == 0 after GC (only node roots it)");
}

static void test_C(void) {
    printf("m3b: C RELATIONSHIP keeps both PERSON values alive\n");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " bob-name: make string! [66 111 98] bob: make person! [1 :bob-name] "
            " spouse: make relationship! [spouse :alice :bob] "
            " alice: none bob: none collect "
            " field field spouse 'from 'id ", mk_int(0),
            "C: relationship.from.id == 0 after GC");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " bob-name: make string! [66 111 98] bob: make person! [1 :bob-name] "
            " spouse: make relationship! [spouse :alice :bob] "
            " alice: none bob: none collect "
            " field field spouse 'to 'id ", mk_int(1),
            "C: relationship.to.id == 1 after GC");
}

static void test_D(void) {
    printf("m3b: D GRAPH-EDGE keeps RELATIONSHIP + two GRAPH-NODEs alive\n");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " bob-name: make string! [66 111 98] bob: make person! [1 :bob-name] "
            " spouse: make relationship! [spouse :alice :bob] "
            " pos-a: make vector! [100 100] "
            " pos-b: make vector! [300 100] "
            " node-a: make graph-node! [:alice :pos-a] "
            " node-b: make graph-node! [:bob :pos-b] "
            " edge-ab: make graph-edge! [:spouse :node-a :node-b] "
            " alice: none bob: none spouse: none pos-a: none pos-b: none "
            " node-a: none node-b: none collect "
            " field field field edge-ab 'relationship 'from 'id ", mk_int(0),
            "D: edge.relationship.from.id == 0 after GC");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " bob-name: make string! [66 111 98] bob: make person! [1 :bob-name] "
            " spouse: make relationship! [spouse :alice :bob] "
            " pos-a: make vector! [100 100] "
            " pos-b: make vector! [300 100] "
            " node-a: make graph-node! [:alice :pos-a] "
            " node-b: make graph-node! [:bob :pos-b] "
            " edge-ab: make graph-edge! [:spouse :node-a :node-b] "
            " alice: none bob: none spouse: none pos-a: none pos-b: none "
            " node-a: none node-b: none collect "
            " field field field edge-ab 'to-node 'position 'x ", mk_int(300),
            "D: edge.to-node.position.x == 300 after GC");
}

static void test_E(void) {
    printf("m3b: E dropping all roots reclaims the entire graph\n");
    int N;
    m3b_run(M3B_TYPES M3B_GRAPH
            " alice: none bob: none charlie: none spouse: none parent1: none parent2: none "
            " pos-a: none pos-b: none pos-c: none node-a: none node-b: none node-c: none "
            " edge-spouse: none edge-parent1: none edge-parent2: none "
            " collect ", &N);
    CHECK(r0_s1_gc_free_cells() >= 15 * 32,
          "E: 15 graph objects (persons/relationships/nodes/edges/vectors) reclaimed");
}

static void test_F(void) {
    printf("m3b: F two diagrams share a PERSON with different VECTORs\n");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " pos-a: make vector! [100 100] pos-b: make vector! [300 100] "
            " n1: make graph-node! [:alice :pos-a] n2: make graph-node! [:alice :pos-b] "
            " collect "
            " field field n1 'position 'x ", mk_int(100),
            "F: n1.position.x == 100");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " pos-a: make vector! [100 100] pos-b: make vector! [300 100] "
            " n1: make graph-node! [:alice :pos-a] n2: make graph-node! [:alice :pos-b] "
            " collect "
            " field field n2 'position 'x ", mk_int(300),
            "F: n2.position.x == 300");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " pos-a: make vector! [100 100] pos-b: make vector! [300 100] "
            " n1: make graph-node! [:alice :pos-a] n2: make graph-node! [:alice :pos-b] "
            " collect "
            " = field field n1 'person 'id field field n2 'person 'id ", mk_int(1),
            "F: both nodes reference the same person (id equal)");
}

static void test_G(void) {
    printf("m3b: G dropping one diagram preserves a shared PERSON\n");
    expect1(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " pos-a: make vector! [100 100] pos-b: make vector! [300 100] "
            " n1: make graph-node! [:alice :pos-a] n2: make graph-node! [:alice :pos-b] "
            " n1: none pos-a: none collect "
            " field field n2 'person 'id ", mk_int(0),
            "G: Alice survives via n2 after n1 is dropped");
    int N;
    m3b_run(M3B_TYPES
            " alice-name: make string! [65 108 105 99 101] alice: make person! [0 :alice-name] "
            " pos-a: make vector! [100 100] pos-b: make vector! [300 100] "
            " n1: make graph-node! [:alice :pos-a] n2: make graph-node! [:alice :pos-b] "
            " n1: none pos-a: none collect ", &N);
    CHECK(r0_s1_gc_free_cells() >= 64,
          "G: dropped node + its private vector reclaimed (>= 64 cells)");
}

/* Host-side SVG decoder (demonstration tooling, not runtime semantics). It
 * consumes the generic surface-command stream and writes an SVG document. It
 * knows ONLY the surface protocol (clear/line/text/rect/present); it knows
 * nothing about the genealogy datatypes. */
static int decode_svg(const long *cmds, int n, char *out, int cap) {
    int len = 0;
    if (len < cap) len += snprintf(out + len, (size_t)(cap - len),
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 400 300\">\n");
    int i = 0;
    while (i < n && len < cap) {
        long op = cmds[i++];
        if (op == 0) { /* clear: fresh document */ }
        else if (op == 1) {
            if (i + 4 > n) break;
            long x1 = cmds[i++], y1 = cmds[i++], x2 = cmds[i++], y2 = cmds[i++];
            len += snprintf(out + len, (size_t)(cap - len),
                "  <line x1=\"%ld\" y1=\"%ld\" x2=\"%ld\" y2=\"%ld\" stroke=\"black\"/>\n",
                x1, y1, x2, y2);
        } else if (op == 2) {
            if (i + 3 > n) break;
            long x = cmds[i++], y = cmds[i++], k = cmds[i++];
            if (i + k > n) break;
            len += snprintf(out + len, (size_t)(cap - len),
                "  <text x=\"%ld\" y=\"%ld\">", x, y);
            for (long j = 0; j < k; j++) {
                long c = cmds[i++];
                char ch = (char)c;
                if (ch == '&') len += snprintf(out + len, (size_t)(cap - len), "&amp;");
                else if (ch == '<') len += snprintf(out + len, (size_t)(cap - len), "&lt;");
                else if (ch == '>') len += snprintf(out + len, (size_t)(cap - len), "&gt;");
                else if (c >= 32 && c < 127) len += snprintf(out + len, (size_t)(cap - len), "%c", ch);
            }
            len += snprintf(out + len, (size_t)(cap - len), "</text>\n");
        } else if (op == 3) {
            if (i + 4 > n) break;
            long x = cmds[i++], y = cmds[i++], w = cmds[i++], h = cmds[i++];
            len += snprintf(out + len, (size_t)(cap - len),
                "  <rect x=\"%ld\" y=\"%ld\" width=\"%ld\" height=\"%ld\" fill=\"white\" stroke=\"black\"/>\n",
                x, y, w, h);
        } else if (op == 4) { break; } /* present */
        else { break; }
    }
    if (len < cap) len += snprintf(out + len, (size_t)(cap - len), "</svg>\n");
    return len;
}

/* run the render program, capture the numeric HOST_PRINT stream into cmds[] */
static int render_stream(long *cmds, int cap) {
    const char *capture = "/tmp/opencode_m3b_stream.txt";
    fflush(stdout);   /* flush pending test output before redirecting fd 1 */
    int saved_out = dup(1);
    int capfd = open(capture, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (capfd >= 0) { dup2(capfd, 1); close(capfd); }

    int N;
    m3b_run(M3B_TYPES M3B_GRAPH M3B_RENDER M3B_RENDER_CALL, &N);

    fflush(stdout);   /* flush the render stream into the capture file */
    if (capfd >= 0) { dup2(saved_out, 1); close(saved_out); }

    int n = 0;
    FILE *fp = fopen(capture, "r");
    if (fp) {
        long v;
        while (n < cap && fscanf(fp, "%ld", &v) == 1) cmds[n++] = v;
        fclose(fp);
    }
    return n;
}

static void test_render(void) {
    printf("m3b: R generic surface command stream -> SVG\n");

    static const long golden[] = {
        0,
        1, 130, 115, 330, 115,
        1, 130, 115, 230, 265,
        1, 330, 115, 230, 265,
        3, 100, 100, 60, 30,
        2, 108, 121, 5, 65, 108, 105, 99, 101,
        3, 300, 100, 60, 30,
        2, 308, 121, 3, 66, 111, 98,
        3, 200, 250, 60, 30,
        2, 208, 271, 7, 67, 104, 97, 114, 108, 105, 101,
        4
    };

    long cmds[256];
    int n = render_stream(cmds, 256);

    int glen = (int)(sizeof golden / sizeof golden[0]);
    int ok = (n == glen);
    if (ok) for (int i = 0; i < n; i++) if (cmds[i] != golden[i]) ok = 0;
    if (ok) printf("  ok: R: command stream matches the golden sequence (%d ints)\n", n);
    else { printf("  FAIL: R: stream mismatch (n=%d want %d)\n", n, glen); failures++; }

    char svg[1 << 16];
    int slen = decode_svg(cmds, n, svg, sizeof svg);
    CHECK(slen > 0, "R: decoder produced SVG text");
    CHECK(strstr(svg, "Alice") != NULL &&
          strstr(svg, "Bob") != NULL &&
          strstr(svg, "Charlie") != NULL,
          "R: SVG contains Alice, Bob and Charlie");
    CHECK(strstr(svg, "<line") != NULL, "R: SVG contains relationship lines");
    CHECK(strstr(svg, "<rect") != NULL && strstr(svg, "<text") != NULL,
          "R: SVG contains node rects and text labels");

    FILE *fp = fopen("examples/m3b-genealogy.svg", "w");
    if (fp) { fwrite(svg, 1, (size_t)slen, fp); fclose(fp); }
    CHECK(fp != NULL, "R: wrote examples/m3b-genealogy.svg");
}

/* Mechanical audit: none of the concrete M3B datatypes may appear in the
 * frozen substrate, the evaluator/parser, the GC, or the host. */
static int file_contains_any(const char *path, const char **needles, int nneedles) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    static char buf[1 << 20];
    size_t nb = fread(buf, 1, sizeof buf - 1, fp);
    buf[nb] = 0;
    fclose(fp);
    for (size_t i = 0; i < nb; i++) buf[i] = (char)tolower((unsigned char)buf[i]);
    int hit = 0;
    for (int k = 0; k < nneedles; k++)
        if (strstr(buf, needles[k])) { hit = 1; break; }
    return hit;
}

static void test_no_leakage(void) {
    printf("m3b: S M3B datatypes absent from frozen substrate/evaluator/GC/HOST\n");
    static const char *files[] = {
        "s1.c", "s1.h", "tests.c", "adversarial.c", "claims.c",
        "r0_s1_runtime.c", "r0_s1.h"
    };
    static const char *names[] = {
        "vector", "person", "relationship", "graph-node", "graph-edge"
    };
    int clean = 1;
    for (int f = 0; f < (int)(sizeof files / sizeof files[0]); f++) {
        if (file_contains_any(files[f], names, (int)(sizeof names / sizeof names[0]))) {
            printf("  FAIL: M3B datatype name leaked into %s\n", files[f]);
            clean = 0;
        }
    }
    CHECK(clean, "S: no M3B datatype name appears in frozen substrate/evaluator/parser/GC/HOST");
}

int run_r0_s1_m3b_tests(void) {
    printf("R0-S1 M3B: genealogy graph rendered as a generic surface stream\n");

    test_A();
    test_B();
    test_C();
    test_D();
    test_E();
    test_F();
    test_G();
    test_render();
    test_no_leakage();

    if (failures == 0) printf("all R0-S1 M3B genealogy-graph tests passed\n");
    return failures;
}
