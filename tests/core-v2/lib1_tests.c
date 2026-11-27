#define _POSIX_C_SOURCE 200809L
/* GLON-LIB-1: the collection and text vocabulary (demo/shop/list.glon,
 * demo/shop/strings-text.glon), outside Glon/View.
 *
 *   lib1_tests_<profile> ROOT            the checks (exit 0: all passed)
 *   lib1_tests_<profile> ROOT eval EXPR  print EXPR's result (a probe)
 *
 * A session loads the prelude, strings, strings-ext, block, list and
 * strings-text, as an ordinary Glon session would. The checks cover empty
 * lists, growth across every capacity boundary the profile can hold, several
 * independent and nested lists, clearing and reuse, collections while lists
 * are live, invalid indices and arguments, and the string operations on empty
 * strings, missing delimiters, long strings and UTF-8 bytes. */
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0, checks = 0;
#define CHECK(c, m) do { checks++; if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static const char *root = ".";
static int load_file(const char *rel) {
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return -1; }
    static char buf[200000];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char *r = buf, *w = buf;
    int in_str = 0;
    while (*r) {
        if (*r == '"') in_str = !in_str;
        if (!in_str && r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    r0_s1_outcome oc;
    int perr = 0;
    cell prog = r0_s1_parse(buf, &perr);
    if (perr) r0_s1_session_parse_error(&oc);
    else r0_s1_session_run_block(prog, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "%s did not load: %s / %s\n", rel, r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail));
        return -2;
    }
    return 0;
}
static int load_text(const char *src) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) return -1;
    r0_s1_outcome o;
    r0_s1_session_run_block(prog, &o);
    return o.status == R0S1_OUT_OK ? 0 : -2;
}
static int fresh(void) {
    r0_s1_init();
    const char *files[] = { "jupyter/prelude.glon", "demo/shop/strings.glon", "demo/shop/strings-ext.glon",
                            "demo/shop/block.glon", "demo/shop/list.glon", "demo/shop/strings-text.glon" };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++)
        if (load_file(files[i]) != 0) return -1;
    return 0;
}

static r0_s1_outcome oc;
static char text[8192];
static const char *ev(const char *src) {
    r0_s1_session_run(src, (unsigned)strlen(src), &oc);
    if (oc.status == R0S1_OUT_OK && oc.count >= 1) r0_s1_mold(r0_s1_result(oc.count - 1, oc.count), text, sizeof text);
    else if (oc.status == R0S1_OUT_OK) snprintf(text, sizeof text, "<no value>");
    else if (oc.status == R0S1_OUT_UNCAUGHT_SIN) {
        char t[64], i[64], a[256];
        r0_s1_mold(oc.sin_type, t, sizeof t); r0_s1_mold(oc.sin_id, i, sizeof i); r0_s1_mold(oc.sin_arg, a, sizeof a);
        snprintf(text, sizeof text, "<sin %s %s %s>", t, i, a);
    } else snprintf(text, sizeof text, "<%s %s>", r0_s1_outcome_status_name(oc.status), r0_s1_outcome_detail_name(oc.detail));
    return text;
}
static int is(const char *src, const char *want) {
    int ok = strcmp(ev(src), want) == 0;
    if (!ok) printf("    got %s, want %s  (for: %s)\n", text, want, src);
    return ok;
}
static long num(const char *src) {
    ev(src);
    return (oc.status == R0S1_OUT_OK && oc.count >= 1) ? (long)int_val(r0_s1_result(oc.count - 1, oc.count)) : -999999;
}
static int sin_is(const char *src, const char *type, const char *id) {
    ev(src);
    char want[160]; snprintf(want, sizeof want, "<sin %s %s ", type, id);
    int ok = strncmp(text, want, strlen(want)) == 0;
    if (!ok) printf("    got %s, want a SIN! %s %s  (for: %s)\n", text, type, id, src);
    return ok;
}
static void collect_now(void) { ev("t/gc 0"); }
static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    if (argc > 1) root = argv[1];
    if (fresh() != 0) { printf("LIB1 FAIL (the libraries did not load)\n"); return 1; }
    if (argc > 3 && !strcmp(argv[2], "eval")) {
        for (int k = 3; k < argc; k++) { double a = now_ms(); ev(argv[k]); printf("%9.1f ms  %.200s\n", now_ms() - a, text); }
        return 0;
    }
    long managed = (long)(GC_HEAP_LIMIT - GC_HEAP_BASE);
    if (load_text("[ t/gc: masm 1 [ CALL collect ARITY 0 EXIT ] ]") != 0) { printf("LIB1 FAIL (t/gc)\n"); return 1; }
    printf("GLON-LIB-1 vocabulary, managed heap %ld cells\n", managed);

    /* ---- lists: empty, growth, boundaries ---- */
    CHECK(is("list/length list/new", "0") && is("list/at list/new 0", "none") && is("list/list? list/new", "1"),
          "L1: an empty list has length 0 and no items");
    CHECK(is("list/list? [ 1 2 ]", "0") && is("list/list? 5", "0") && is("list/list? \"s\"", "0")
          && is("list/list? reduce [ 0 0 0 0 ]", "0"), "L2: list/list? is 0 for blocks, integers, strings");
    CHECK(is("t/l: list/new  list/append t/l 10  list/append t/l \"x\"  list/append t/l none  list/length t/l", "3")
          && is("list/at t/l 0", "10") && is("list/at t/l 1", "\"x\"") && is("list/at t/l 2", "none")
          && is("list/at t/l 3", "none") && is("list/at t/l -1", "none"),
          "L3: append then at: integers, strings and NONE; outside the list is NONE");
    /* growth across every boundary the profile holds (64, 4,096 and, with room, 262,144 items) */
    char want[64];
    long big = managed >= 60000 ? 4200 : 300;   /* 4,200: past the 4,096 boundary, into the generic (span 4,096) tree path */
    char src[512];
    snprintf(src, sizeof src, "t/g: list/new  t/i: 0  loop/until func [] [ list/append t/g * t/i 3  t/i: + t/i 1  = t/i %ld ]  list/length t/g", big);
    double t0 = now_ms();
    long got = num(src);
    double t1 = now_ms();
    snprintf(src, sizeof src, "L4: %ld appends (%.0f ms, %.1f us each)", big, t1 - t0, (t1 - t0) * 1000.0 / big);
    CHECK(got == big, src);
    int ok = 1;
    long probes[] = { 0, 1, 62, 63, 64, 65, 127, 128, 4094, 4095, 4096, 4097, 262142, 262143, 262144, 262145, big - 1 };
    for (size_t k = 0; k < sizeof probes / sizeof probes[0]; k++) {
        if (probes[k] >= big) continue;
        snprintf(src, sizeof src, "list/at t/g %ld", probes[k]);
        if (num(src) != probes[k] * 3) { ok = 0; printf("    item %ld is %s\n", probes[k], text); }
    }
    snprintf(src, sizeof src, "list/at t/g %ld", big);
    CHECK(ok && is(src, "none"), "L5: every item either side of each capacity boundary reads back; one past the end is NONE");
    snprintf(src, sizeof src, "t/s: 0  list/each t/g func [x] [ t/s: + t/s x ]  t/s");
    t0 = now_ms(); got = num(src); t1 = now_ms();
    snprintf(src, sizeof src, "L6: list/each visits every item once, in order (sum; %.0f ms, %.1f us an item)", t1 - t0, (t1 - t0) * 1000.0 / big);
    CHECK(got == 3 * (big - 1) * big / 2, src);
    CHECK(num("list/find t/g func [x] [ = x 192 ]") == 64 && num("list/find t/g func [x] [ = x -1 ]") == -1
          && num("list/find t/g func [x] [ 1 ]") == 0, "L7: list/find answers the first match, or -1");
    snprintf(src, sizeof src, "list/length list/filter t/g func [x] [ = 0 - x * / x 2 2 ]");
    CHECK(num(src) == (big + 1) / 2, "L8: list/filter keeps the items for which the function is truthy");
    /* put, remove, clear and reuse */
    CHECK(is("list/put t/l 1 \"y\"", "\"y\"") && is("list/at t/l 1", "\"y\"") && is("list/length t/l", "3"),
          "L9: list/put replaces an item; the length is unchanged");
    CHECK(sin_is("list/put t/l 3 0", "range", "list/put") && sin_is("list/put t/l -1 0", "range", "list/put")
          && sin_is("list/put list/new 0 0", "range", "list/put"), "L10: list/put outside the list raises 'range");
    CHECK(sin_is("list/at t/l \"a\"", "type", "list/at") && sin_is("list/at [ 1 ] 0", "type", "list/at")
          && sin_is("list/append 5 1", "type", "list/append") && sin_is("list/length \"x\"", "type", "list/length")
          && sin_is("list/each none func [x] [ x ]", "type", "list/each"),
          "L11: a non-list or a non-integer index raises 'type");
    CHECK(is("t/r: list/from [ 1 2 3 4 5 ]  list/remove t/r 1", "2") && is("list/length t/r", "4")
          && is("list/at t/r 1", "3") && is("list/at t/r 3", "5") && is("list/at t/r 4", "none")
          && is("list/remove t/r 3", "5") && is("list/remove t/r 0", "1") && is("list/length t/r", "2")
          && is("list/at t/r 0", "3") && sin_is("list/remove t/r 2", "range", "list/remove")
          && sin_is("list/remove list/new 0", "range", "list/remove"),
          "L12: list/remove shifts the later items down; the first and last can go; outside raises 'range");
    snprintf(src, sizeof src, "t/x: list/from [ 7 ]  t/i: 0  loop/until func [] [ list/append t/x t/i  t/i: + t/i 1  = t/i 200 ]  list/remove t/x 0");
    CHECK(num(src) == 7 && num("list/at t/x 0") == 0 && num("list/at t/x 63") == 63 && num("list/at t/x 64") == 64
          && num("list/at t/x 199") == 199 && num("list/length t/x") == 200, "L13: a removal moves items across leaves");
    CHECK(is("list/length list/clear t/r", "0") && is("list/at t/r 0", "none")
          && is("list/append t/r \"again\"  list/at t/r 0", "\"again\"") && is("list/length t/r", "1"),
          "L14: list/clear empties a list; it can be used again");
    /* independent and nested lists */
    CHECK(is("t/a: list/new  t/b: list/new  list/append t/a 1  list/append t/b 2  list/append t/b 3  "
             "s/+ s/from-int list/length t/a s/from-int list/length t/b", "\"12\"")
          && is("list/at t/a 0", "1") && is("list/at t/b 0", "2"), "L15: independent lists do not share items");
    CHECK(is("t/n: list/new  list/append t/n list/from [ 1 2 ]  list/append t/n list/new  list/append list/at t/n 1 \"in\"  "
             "list/at list/at t/n 1 0", "\"in\"") && is("list/at list/at t/n 0 1", "2")
          && is("list/length list/at t/n 1", "1"), "L16: lists hold lists");
    CHECK(is("block-len list/to-block list/new", "0") && is("list/to-block list/from [ 1 2 3 ]", "[ 1 2 3 ]")
          && is("block-len list/blank 64", "64") && is("list/blank 0", "[ ]") && sin_is("list/blank 65", "range", "list/blank")
          && sin_is("list/blank -1", "range", "list/blank") && sin_is("list/to-block t/g", "range", "list/to-block"),
          "L22: list/to-block and list/blank (0 to 64 items; more raises 'range)");
    CHECK(is("list/to-block list/slice t/g 63 3", "[ 189 192 195 ]") && is("list/length list/slice t/g 0 0", "0")
          && sin_is("list/slice t/g -1 2", "range", "list/slice") && sin_is("list/slice list/from [ 1 ] 0 2", "range", "list/slice")
          && sin_is("list/slice list/from [ 1 ] 1 1", "range", "list/slice") && is("list/length list/slice list/from [ 1 ] 1 0", "0"),
          "L23: list/slice: a range of items; a range outside the list raises 'range");
    CHECK(is("list/to-block list/map list/from [ 1 2 3 ] func [x] [ * x x ]", "[ 1 4 9 ]")
          && is("list/length list/map list/new func [x] [ x ]", "0"), "L24: list/map");
    CHECK(is("t/i: 0  loop/for 0 100 func [i] [ t/i: + t/i i ]  t/i", "4950") && is("t/i: 0  loop/for 5 5 func [i] [ t/i: 1 ]  t/i", "0")
          && is("t/i: 0  loop/for 0 3000 func [i] [ t/i: + t/i 1 ]  t/i", "3000"), "L25: loop/for visits lo <= i < hi (depth bounded)");
    CHECK(is("t/u: list/from [ 1 2 3 4 5 ]  list/length list/truncate t/u 2", "2") && is("list/at t/u 1", "2") && is("list/at t/u 2", "none")
          && is("list/append t/u 9  list/at t/u 2", "9") && is("list/length list/truncate t/u 0", "0")
          && sin_is("list/truncate t/u 1", "range", "list/truncate") && sin_is("list/truncate t/u -1", "range", "list/truncate")
          && sin_is("list/truncate 5 0", "type", "list/truncate"),
          "L26: list/truncate keeps the first n items (the storage kept); outside [0, length] raises 'range");
    /* sorting and collections: a fresh session (the 64K loader heap keeps
     * every parsed source, so the checks come in groups); the lists they use
     * are made again */
    printf("  loader heap pointer after the first list checks: %ld\n", (long)M[GC_LOADER_HP]);
    if (fresh() != 0 || load_text("[ t/gc: masm 1 [ CALL collect ARITY 0 EXIT ] ]") != 0) { printf("LIB1 FAIL (session 2)\n"); return 1; }
    snprintf(src, sizeof src, "t/g: list/new  t/i: 0  loop/until func [] [ list/append t/g * t/i 3  t/i: + t/i 1  = t/i %ld ]  "
                              "t/l: list/from reduce [ 10 \"y\" none ]  t/b: list/from [ 2 3 ]  t/n: list/new  list/append t/n list/from [ 1 2 ]  "
                              "list/append t/n list/from reduce [ \"in\" ]  list/length t/g", big);
    CHECK(num(src) == big, "L17-: the lists for the next group made again");
    /* sorting */
    CHECK(is("s/join list/sort s/split \"pear,fig,apple,kiwi,date\" 44 func [a b] [ < s/at a 0 s/at b 0 ] \",\"",
             "\"apple,date,fig,kiwi,pear\"")
          && is("list/length list/sort list/new func [a b] [ 1 ]", "0")
          && is("list/at list/sort list/from [ 9 ] func [a b] [ < a b ] 0", "9"),
          "L17: list/sort orders by the function; empty and one-item lists");
    CHECK(is("t/p: list/sort list/from reduce [ reduce [ 2 \"a\" ] reduce [ 1 \"b\" ] reduce [ 2 \"c\" ] reduce [ 1 \"d\" ] ] "
             "func [a b] [ < block-at a 0 block-at b 0 ]  "
             "s/join reduce [ block-at list/at t/p 0 1 block-at list/at t/p 1 1 block-at list/at t/p 2 1 block-at list/at t/p 3 1 ] \"\"",
             "\"bdac\""), "L18: list/sort is stable (equal keys keep their order)");
    long ns = managed >= 60000 ? 200 : 40;
    snprintf(src, sizeof src, "t/q: list/sort list/filter t/g func [x] [ < x %ld ] func [a b] [ > a b ]  "
                              "list/find t/q func [x] [ = x 0 ]", ns * 3);
    t0 = now_ms(); got = num(src); t1 = now_ms();
    snprintf(src, sizeof src, "L19: %ld items sorted in descending order (%.0f ms)", ns, t1 - t0);
    snprintf(want, sizeof want, "list/at t/q 0");
    CHECK(got == ns - 1 && num(want) == ns * 3 - 3, src);
    /* collections while lists are live */
    CHECK(is("t/g: 0  t/q: 0  0", "0"), "L20: (the large lists dropped first)");
    long g0 = r0_s1_gc_count();
    snprintf(src, sizeof src, "t/i: 0  loop/until func [] [ list/append t/l reduce [ t/i s/from-int t/i ]  t/j: s/+ \"garbage\" s/from-int t/i  "
                              "t/i: + t/i 1  = t/i %ld ]  list/length t/l", managed / 200);
    got = num(src);
    long g1 = r0_s1_gc_count();
    ok = got > 3;
    for (long k = 3; k < got && ok; k += (got / 97) + 1) {
        snprintf(src, sizeof src, "s/= block-at list/at t/l %ld 1 s/from-int %ld", k, k - 3);
        if (num(src) != 1) { ok = 0; printf("    item %ld: %s\n", k, text); }
    }
    snprintf(src, sizeof src, "L20: %ld items stay valid through %ld collection(s) made during the appends", got, g1 - g0);
    CHECK(ok && g1 > g0 && is("list/at t/l 0", "10") && is("list/at list/at t/n 1 0", "\"in\""), src);
    collect_now();
    long live = r0_s1_gc_live_cells();
    CHECK(is("t/l: 0  0", "0"), "L21: dropping a large list");
    collect_now();
    long live2 = r0_s1_gc_live_cells();
    snprintf(src, sizeof src, "L21: a dropped list is collected (live %ld -> %ld cells)", live, live2);
    CHECK(live2 < live && is("list/length t/b", "2"), src);

    /* ---- strings (a fresh session: the 64K loader heap, which keeps every
     * parsed source, holds one group of checks at a time) ---- */
    printf("  loader heap pointer after the list checks: %ld\n", (long)M[GC_LOADER_HP]);
    if (fresh() != 0 || load_text("[ t/gc: masm 1 [ CALL collect ARITY 0 EXIT ] ]") != 0) { printf("LIB1 FAIL (second session)\n"); return 1; }
    CHECK(is("s/find-byte \"hello\" 0 108", "2") && is("s/find-byte \"hello\" 3 108", "3") && is("s/find-byte \"hello\" 4 108", "5")
          && is("s/find-byte \"\" 0 32", "0") && is("s/find-byte \"hello\" -5 104", "0") && is("s/find-byte \"hello\" 99 104", "5"),
          "S1: s/find-byte: the first index >= i, or the length; the start is clamped");
    CHECK(is("s/find-range \"abc123\" 0 48 57", "3") && is("s/find-range \"abc\" 0 48 57", "3")
          && is("s/find-range \"xyzA\" 0 65 90", "3") && is("s/find-range \"\" 0 0 255", "0"),
          "S2: s/find-range finds the first byte in [lo, hi]");
    CHECK(is("s/find \"hello world\" 0 \"wor\"", "6") && is("s/find \"hello world\" 0 \"word\"", "11")
          && is("s/find \"aaab\" 0 \"ab\"", "2") && is("s/find \"abc\" 1 \"\"", "1") && is("s/find \"\" 0 \"a\"", "0")
          && is("s/find \"abcabc\" 1 \"abc\"", "3") && is("s/find \"ab\" 0 \"abc\"", "2"),
          "S3: s/find finds a string, or answers the length");
    CHECK(is("s/starts-with? \"hello\" \"he\"", "1") && is("s/starts-with? \"hello\" \"hello\"", "1")
          && is("s/starts-with? \"he\" \"hello\"", "0") && is("s/starts-with? \"hello\" \"\"", "1")
          && is("s/starts-with? \"\" \"\"", "1") && is("s/starts-with? \"hello\" \"hx\"", "0"),
          "S4: s/starts-with?");
    CHECK(is("list/length s/split \"\" 9", "1") && is("list/at s/split \"\" 9 0", "\"\"")
          && is("list/length s/split \"abc\" 9", "1") && is("list/at s/split \"abc\" 9 0", "\"abc\"")
          && is("list/length s/split \"a,b,\" 44", "3") && is("list/at s/split \"a,b,\" 44 2", "\"\"")
          && is("list/length s/split \",\" 44", "2") && is("s/join s/split \",,a,,\" 44 \"|\"", "\"||a||\""),
          "S5: s/split: empty strings, no delimiter, delimiters at either end and side by side");
    CHECK(is("s/join list/new \", \"", "\"\"") && is("s/join list/from reduce [ \"a\" ] \", \"", "\"a\"")
          && is("s/join reduce [ \"a\" \"b\" \"c\" ] \", \"", "\"a, b, c\"") && is("s/join reduce [ \"\" \"\" ] \"\"", "\"\"")
          && sin_is("s/join reduce [ \"a\" 5 ] \"\"", "type", "s/join") && sin_is("s/join 5 \"\"", "type", "s/join")
          && sin_is("s/join list/new 5", "type", "s/join"),
          "S6: s/join a list or a block; a non-string item raises 'type");
    CHECK(is("s/lower \"Hello WORLD 123 @[`{\"", "\"hello world 123 @[`{\"") && is("s/upper \"Hello world 123 @[`{\"", "\"HELLO WORLD 123 @[`{\"")
          && is("s/lower \"\"", "\"\"") && is("s/upper \"\"", "\"\""), "S7: ASCII case: only A-Z / a-z change (@ [ ` { are next to them)");
    CHECK(is("s/length s/lower \"\xc3\x84\xc3\xa9\xcf\x80\"", "6") && is("s/= s/lower \"\xc3\x84\xc3\xa9\xcf\x80\" \"\xc3\x84\xc3\xa9\xcf\x80\"", "1")
          && is("s/= s/upper \"\xc3\xa9t\xc3\xa9\" \"\xc3\xa9T\xc3\xa9\"", "1"),
          "S8: UTF-8 bytes are copied unchanged (\"\xc3\x84\xc3\xa9\xcf\x80\" stays as it is; only the ASCII t of \"\xc3\xa9t\xc3\xa9\" changes)");
    CHECK(is("s/find-byte \"\xcf\x80x\" 0 120", "2") && is("list/length s/split \"a\xcf\x80" "b\" 128", "2")
          && is("s/find \"\xce\xb1\xce\xb2\xce\xb3\" 0 \"\xce\xb3\"", "4"),
          "S9: UTF-8 is searched as bytes: indices are byte offsets (x is at byte 2 after the 2-byte pi)");
    CHECK(sin_is("s/find-byte 5 0 1", "type", "s/find-byte") && sin_is("s/find-byte \"a\" \"0\" 1", "type", "s/find-byte")
          && sin_is("s/split \"a\" \",\"", "type", "s/split") && sin_is("s/lower 5", "type", "s/lower")
          && sin_is("s/find \"a\" 0 5", "type", "s/find") && sin_is("s/starts-with? \"a\" none", "type", "s/starts-with?"),
          "S10: wrong argument types raise 'type");
    /* long strings: built by doubling (4,096 bytes; 65,536 with room), then searched, split, joined, cased */
    long nb = managed >= 60000 ? managed / 64 : 96;
    snprintf(src, sizeof src, "t/s: \"aB,\"  t/i: 3  loop/until func [] [ t/s: s/+ t/s t/s  t/i: * t/i 2  >= t/i %ld ]  s/length t/s", nb);
    long len = num(src);
    CHECK(len >= nb, "S11: a long string");
    snprintf(src, sizeof src, "s/find-byte t/s 0 120");
    t0 = now_ms(); got = num(src); t1 = now_ms();
    snprintf(src, sizeof src, "S12: s/find-byte over %ld bytes, not found: the length (%.2f ms)", len, t1 - t0);
    CHECK(got == len, src);
    t0 = now_ms(); got = num("s/find t/s 0 \",x\""); t1 = now_ms();
    snprintf(src, sizeof src, "S13: s/find over %ld bytes with a candidate every 3 bytes, not found (%.0f ms; depth stays bounded)", len, t1 - t0);
    CHECK(got == len, src);
    t0 = now_ms(); got = num("list/length s/split t/s 44"); t1 = now_ms();
    snprintf(src, sizeof src, "S14: s/split into %ld pieces (%.0f ms)", len / 3 + 1, t1 - t0);
    CHECK(got == len / 3 + 1, src);
    t0 = now_ms(); ok = is("s/= s/join s/split t/s 44 \",\" t/s", "1"); t1 = now_ms();
    snprintf(src, sizeof src, "S15: s/join of the pieces is the string again (%.0f ms)", t1 - t0);
    CHECK(ok, src);
    got = num("s/find-byte s/lower t/s 0 66");
    CHECK(got == len && num("s/find-byte s/upper t/s 0 97") == len && num("s/length s/upper t/s") == len
          && num("s/find-byte t/s 0 66") == 1, "S16: s/lower / s/upper of a long string (the original unchanged)");
    CHECK(is("t/s: 0  0", "0"), "S17: (the long string dropped)");
    /* ---- X: the examples of GLON-LIB-1.md section 2, exactly as written there ---- */
    if (fresh() != 0) { printf("LIB1 FAIL (examples session)\n"); return 1; }
    struct { const char *src, *want; } ex[] = {
        { "items: list/new  list/length items", "0" },
        { "list/append items \"pear\"  list/length items", "1" },
        { "list/append items \"fig\"  list/length items", "2" },
        { "list/at items 0", "\"pear\"" }, { "list/at items 5", "none" },
        { "list/put items 1 \"kiwi\"", "\"kiwi\"" },
        { "list/find items func [x] [ s/= x \"kiwi\" ]", "1" },
        { "list/to-block list/sort items func [a b] [ < s/at a 0 s/at b 0 ]", "[ \"kiwi\" \"pear\" ]" },
        { "list/remove items 0", "\"pear\"" }, { "list/length list/clear items", "0" },
        { "s/find-byte \"a,b\" 0 44", "1" }, { "s/find-range \"abc123\" 0 48 57", "3" },
        { "s/find \"hello world\" 0 \"wor\"", "6" }, { "s/find \"hello\" 0 \"xyz\"", "5" },
        { "s/starts-with? \"hello\" \"he\"", "1" }, { "s/join s/split \"a,b,,c\" 44 \"|\"", "\"a|b||c\"" },
        { "list/length s/split \"\" 44", "1" }, { "s/lower \"Kwok Alice\"", "\"kwok alice\"" },
        { "s/upper \"\xc3\xa9t\xc3\xa9\"", "\"\xc3\xa9T\xc3\xa9\"" }, { "s/find-byte \"\xcf\x80x\" 0 120", "2" },
        { "loop/collect 0 3 func [i] [ * i i ]", "[ 0 1 4 ]" },
    };
    ok = 1;
    for (size_t k = 0; k < sizeof ex / sizeof ex[0]; k++) if (!is(ex[k].src, ex[k].want)) ok = 0;
    CHECK(ok, "X1: every example of GLON-LIB-1.md section 2 gives the result shown there");
    collect_now();
    printf("  heap after the checks: %ld live cells of %ld; %ld collections\n", r0_s1_gc_live_cells(), managed, r0_s1_gc_count());
    printf("LIB1 %s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
