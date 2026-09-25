/* r0_s1_string_ops_tests.c - immutable STRING! operations.
 *
 *   s/+ a b       a NEW string: a's bytes then b's (neither input is changed)
 *   s/= a b       1 if a and b hold the same bytes, else 0 (str-eq is the same word)
 *   s/length a    the number of stored bytes
 *   s/print a     a's bytes then a newline on stdout; no value
 *
 * A STRING! is immutable: a managed payload [length, byte0 .. byteN-1], one raw
 * byte per cell. An interior '/' marks a qualified name (domain/operation);
 * for now `s/+` and friends are ordinary (flat) words, parsed as one word
 * each, and a bare `/` is still integer division.
 *
 * The operations are library code (demo/shop/strings.glon, over common.glon's
 * str-eq); no runtime native is involved. Each group runs in a persistent
 * session with bootstrap + strings loaded (as the primer does), so string
 * literals ("..." -> mk-string) work. */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char buf[70000];
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char *r = buf, *w = buf;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    int err = 0;
    cell b = r0_s1_parse(buf, &err);
    if (err) return -2;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -3;
}

static int fresh(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    if (load_file("demo/shop/bootstrap.glon") != 0) return -1;
    return load_file("demo/shop/strings.glon");
}

/* run a cell in the session; `text` gets the molded single result (or the
 * outcome status name) */
static r0_s1_outcome oc;
static char text[512];
static const char *cell_(const char *src) {
    r0_s1_session_run(src, (unsigned)strlen(src), &oc);
    if (oc.status == R0S1_OUT_OK && oc.count == 1) r0_s1_mold(r0_s1_result(0, 1), text, sizeof text);
    else if (oc.status == R0S1_OUT_OK) snprintf(text, sizeof text, "<%d values>", oc.count);
    else snprintf(text, sizeof text, "<%s>", r0_s1_outcome_status_name(oc.status));
    return text;
}
static int is(const char *src, const char *want) { return strcmp(cell_(src), want) == 0; }
static int sin_is(const char *src, const char *type, const char *id) {
    char t[64], i[64];
    cell_(src);
    if (oc.status != R0S1_OUT_UNCAUGHT_SIN) return 0;
    r0_s1_mold(oc.sin_type, t, sizeof t);
    r0_s1_mold(oc.sin_id, i, sizeof i);
    return !strcmp(t, type) && !strcmp(i, id);
}

int run_r0_s1_string_ops_tests(void) {
    printf("R0-S1 immutable STRING! operations (s/+ s/= s/length s/print)\n");

    /* ---- qualified spelling: an interior '/' is part of an ordinary word ------ */
    {
        r0_s1_init();
        int err = 0;
        cell b = r0_s1_parse("[ s/+ s/+: :s/+ 's/+ s/= s/length s/print / x: :x 'x a/b/c ]", &err);
        cell p = r0_untag(b);
        enum { NQ = 12 };
        int tags_ok = !err && M[p] == NQ;
        static const int want_tag[NQ] = { T_WORD, T_SET, T_GET, T_LIT, T_WORD, T_WORD, T_WORD,
                                          T_WORD, T_SET, T_GET, T_LIT, T_WORD };
        static const char *want_name[NQ] = { "s/+", "s/+", "s/+", "s/+", "s/=", "s/length", "s/print",
                                             "/", "x", "x", "x", "a/b/c" };
        for (int i = 0; tags_ok && i < NQ; i++) {
            cell v = M[p + BLK_DATA + i];
            const char *nm = r0_s1_sym_name(word_id(v));
            if ((v & 15) != (cell)want_tag[i] || !nm || strcmp(nm, want_name[i])) tags_ok = 0;
        }
        CHECK(tags_ok, "Q1: `s/+` is one word; `s/+:` / `:s/+` / `'s/+` are its set/get/lit forms; "
                       "`s/=` `s/length` `s/print` are words; bare `/` is its own word; "
                       "`x:` `:x` `'x` unchanged; `a/b/c` is one word");
    }

    CHECK(fresh() == 0, "setup: bootstrap loads");
    CHECK(is("/ 7 2", "3") && is("n: 12  / n 4", "3"), "Q2: bare `/` is still integer division (7 / 2 -> 3)");

    /* ---- A: inputs survive concatenation unchanged ------------------------- */
    CHECK(is("a: \"red\"  b: \" glon\"  c: s/+ a b  c", "\"red glon\""), "A1: c: s/+ a b -> \"red glon\"");
    CHECK(is("a", "\"red\"") && is("b", "\" glon\""), "A2: a and b are unchanged (\"red\", \" glon\")");
    CHECK(is("= c s/+ a b", "0"), "A3: every s/+ returns a NEW string (= is identity: a second s/+ is a different value)");
    CHECK(is("s/length c", "8") && is("s/length a", "3"), "A4: s/length counts bytes (8, 3)");

    /* ---- E, F: equality and empty strings ----------------------------------- */
    CHECK(is("s/= s/+ \"ab\" \"c\" s/+ \"a\" \"bc\"", "1"),
          "E1: s/= compares contents: two distinct allocations with the same bytes are equal");
    CHECK(is("s/= \"abc\" \"abd\"", "0") && is("s/= \"abc\" \"ab\"", "0"),
          "E2: different bytes, or different lengths, are not equal");
    CHECK(is("str-eq \"abc\" \"abc\"", "1") && is("str-eq \"abc\" \"abd\"", "0") && is("= :str-eq :s/=", "1"),
          "E3: str-eq is another name for s/= (one equality algorithm)");
    CHECK(is("s/+ \"\" \"\"", "\"\"") && is("s/length \"\"", "0") && is("s/+ \"\" \"x\"", "\"x\"")
          && is("s/+ \"x\" \"\"", "\"x\"") && is("s/= \"\" \"\"", "1"),
          "F: empty strings concatenate, measure and compare correctly");
    CHECK(is("s/+ c c", "\"red glonred glon\"") && is("c", "\"red glon\""),
          "F2: a string concatenated with itself; the input is unchanged");

    /* ---- raw bytes: no C-string semantics ----------------------------------- */
    CHECK(is("z: mk-string [104 0 255 105]  s/length z", "4"),
          "B1: bytes are stored as-is: a string with a 0 byte and a 255 byte has length 4");
    CHECK(is("s/length s/+ z z", "8") && is("s/= s/+ z \"\" z", "1") && is("s/= z mk-string [104 0 255 106]", "0"),
          "B2: s/+ copies embedded 0/255 bytes exactly; s/= sees the last byte differ");
    CHECK(is("dbl: func [s n] [ either > n 0 [ dbl s/+ s s  - n 1 ] [ s ] ]  w: dbl \"ab\" 7  s/length w", "256")
          && is("s/= w dbl \"ab\" 7", "1") && is("s/= w s/+ dbl \"ab\" 6 dbl \"ac\" 6", "0"),
          "B3: long strings (256 bytes) concatenate and compare, with no recursion per byte");

    /* ---- C: a closure captures a STRING! and uses it later ------------------ */
    CHECK(is("greet: func [name] [ does [ s/+ \"hi \" name ] ]  g: greet \"glon\"  g", "\"hi glon\""),
          "C1: a closure captures a string argument and builds a new string later");
    CHECK(is("g", "\"hi glon\""), "C2: calling it again gives an equal, fresh result");

    /* ---- D: GC pressure preserves every live string ------------------------ */
    {
        long gc0 = r0_s1_gc_count();
        cell_("acc: \"\"  grow: func [n] [ either > n 0 [ acc: s/+ acc \"x\"  grow - n 1 ] [ 0 ] ]  grow 60");
        cell_("junk: func [n] [ either > n 0 [ s/+ c c  s/+ acc acc  junk - n 1 ] [ 0 ] ]"
              "  spin: func [n] [ either > n 0 [ junk 40  spin - n 1 ] [ 0 ] ]  spin 40");
        long gcs = r0_s1_gc_count() - gc0;
        char msg[160];
        snprintf(msg, sizeof msg, "D1: 3,200 garbage concatenations forced %ld collections", gcs);
        CHECK(oc.status == R0S1_OUT_OK && gcs >= 3, msg);
        CHECK(is("a", "\"red\"") && is("b", "\" glon\"") && is("c", "\"red glon\"") && is("g", "\"hi glon\""),
              "D2: every live string (a, b, c, the closure's) survived collection unchanged");
        CHECK(is("s/length acc", "60") && is("s/= acc s/+ s/+ s/+ s/+ s/+ s/+ \"xxxxxxxxxx\" \"xxxxxxxxxx\""
                 " \"xxxxxxxxxx\" \"xxxxxxxxxx\" \"xxxxxxxxxx\" \"xxxxxxxxxx\" \"\"", "1"),
              "D3: a string built by 60 successive s/+ is intact: 60 bytes, all x");
    }

    /* ---- G: bad argument types ----------------------------------------------- */
    CHECK(sin_is("s/+ 1 \"x\"", "type", "s/+") && sin_is("s/+ \"x\" [x]", "type", "s/+"),
          "G1: s/+ with a non-STRING! raises SIN! 'type 's/+ (either argument)");
    CHECK(sin_is("s/= 1 2", "type", "s/=") && sin_is("str-eq \"a\" 'w", "type", "s/=")
          && sin_is("s/length none", "type", "s/length") && sin_is("s/print 5", "type", "s/print"),
          "G2: s/=, str-eq, s/length and s/print raise SIN! 'type too");
    CHECK(is("e: judge [ s/+ \"x\" 7 ]  sin-arg e", "7"), "G3: judge catches it; the SIN!'s arg is the offending value");
    CHECK(is("c", "\"red glon\"") && is("s/length acc", "60"), "G4: session state is untouched by the failures");
    CHECK(sin_is("s/+ 1 s/length 2", "type", "s/length") && sin_is("str-eq 1 2", "type", "s/=")
          && is("e: judge [ s/= 1 \"x\" ]  sin-arg e", "1") && is("e: judge [ s/= \"x\" 2 ]  sin-arg e", "2"),
          "G5: the operations are functions: both arguments are evaluated, then the first non-string is reported");

    /* ---- H: strings survive later failed cells ---------------------------------- */
    cell_("y: [ unclosed");
    int parse_failed = oc.status == R0S1_OUT_PARSE_ERROR;
    cell_("undefined-word");
    int halted = oc.status == R0S1_OUT_HALT;
    CHECK(parse_failed && halted && is("s/+ c \"!\"", "\"red glon!\""),
          "H: after a parse error and a machine halt, strings are intact and usable");

    /* ---- s/print returns no value --------------------------------------------- */
    CHECK(is("s/print \"\"", "<0 values>") && oc.status == R0S1_OUT_OK,
          "P: s/print returns no value (like print); its output is checked by the host tests");

    /* ---- without strings.glon (bootstrap only, like the traffic page) -------- */
    {
        cell me = r0_s1_init();
        M[M1_MAIN_ENTRY_CELL] = me;
        M[M1_CURSOR] = 0;
        M[M1_STRESS_CNT] = 0;
        for (int i = 0; i < M1_MAX_TASKS; i++)
            M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
        CHECK(load_file("demo/shop/bootstrap.glon") == 0, "setup: bootstrap alone loads");
        CHECK(is("str-eq \"abc\" \"abc\"", "1") && is("str-eq \"abc\" \"abd\"", "0")
              && sin_is("str-eq \"a\" 'w", "type", "s/=") && sin_is("str-eq 5 \"a\"", "type", "s/="),
              "W1: without strings.glon, str-eq still compares strings and raises SIN! 'type 's/=");
        cell_("s/+ \"a\" \"b\"");
        CHECK(oc.status == R0S1_OUT_HALT, "W2: without strings.glon, s/+ is not defined (the runtime has no string natives)");
    }

    if (failures == 0) printf("all string operation tests passed\n");
    return failures;
}
