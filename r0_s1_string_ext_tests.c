/* r0_s1_string_ext_tests.c - the optional STRING! extension (demo/shop/strings-ext.glon).
 *
 * Approved API (2026-11-07). All indices are 0-based byte offsets. The
 * invalid-range cases (B5-B8, E6-E9) guard the only checks that stand between
 * callers and the unchecked masm workers: keep them.
 *   s/at s i           byte at 0-based i, or NONE outside [0, length)
 *   s/slice s start n  a new string of n bytes from start; a bad range raises 'range
 *   s/span s start end a new string of the bytes in [start, end) (half-open);
 *                      a bad range raises 'range
 *   s/scan-set s i set first index >= i whose byte is not in `set`; i clamped
 *   s/from-int n       decimal text of n
 *
 * Library code over unchecked masm workers, loaded AFTER strings.glon by the
 * sessions that need it (strings.glon itself is unchanged). The cases cover
 * empty strings, every boundary, invalid indices and argument types,
 * immutability, the integer range, and the byte (not character) semantics of
 * UTF-8 text. Each group runs in a persistent session with bootstrap + strings
 * + strings-ext loaded (as the primer loads bootstrap + strings). */

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
    if (load_file("demo/shop/strings.glon") != 0) return -2;
    return load_file("demo/shop/strings-ext.glon");
}

static r0_s1_outcome oc;
static char text[1024];
static const char *cell_(const char *src) {
    r0_s1_session_run(src, (unsigned)strlen(src), &oc);
    if (oc.status == R0S1_OUT_OK && oc.count == 1) r0_s1_mold(r0_s1_result(0, 1), text, sizeof text);
    else if (oc.status == R0S1_OUT_OK) snprintf(text, sizeof text, "<%d values>", oc.count);
    else snprintf(text, sizeof text, "<%s>", r0_s1_outcome_status_name(oc.status));
    return text;
}
static int is(const char *src, const char *want) {
    int ok = strcmp(cell_(src), want) == 0;
    if (!ok) printf("    got %s, want %s  (for: %s)\n", text, want, src);
    return ok;
}
static int sin_is(const char *src, const char *type, const char *id) {
    char t[64], i[64];
    cell_(src);
    if (oc.status != R0S1_OUT_UNCAUGHT_SIN) { printf("    no SIN! (%s) for: %s\n", text, src); return 0; }
    r0_s1_mold(oc.sin_type, t, sizeof t);
    r0_s1_mold(oc.sin_id, i, sizeof i);
    int ok = strcmp(t, type) == 0 && strcmp(i, id) == 0;
    if (!ok) printf("    SIN! %s %s, want %s %s  (for: %s)\n", t, i, type, id, src);
    return ok;
}

int run_r0_s1_string_ext_tests(void) {
    printf("R0-S1 STRING! extension (strings-ext.glon: s/at s/slice s/span s/scan-set s/from-int)\n");
    CHECK(fresh() == 0, "X0: bootstrap + strings + strings-ext load in one session");

    /* ---- the existing string semantics are unchanged ---- */
    CHECK(is("s/+ \"ab\" \"cd\"", "\"abcd\"") && is("s/length \"hello\"", "5")
          && is("s/= \"ab\" s/+ \"a\" \"b\"", "1"),
          "X1: s/+ s/length s/= behave as before with the extension loaded");

    /* ---- s/at ---- */
    CHECK(is("s/at \"abc\" 0", "97") && is("s/at \"abc\" 2", "99"), "A1: s/at reads the first and last bytes (0-based)");
    CHECK(is("s/at \"abc\" 3", "none") && is("s/at \"abc\" -1", "none") && is("s/at \"abc\" 1000000", "none"),
          "A2: s/at is NONE at the length, below 0 and far beyond the end");
    CHECK(is("s/at \"\" 0", "none") && is("s/at \"\" -1", "none"), "A3: every index into an empty string is NONE");
    CHECK(is("s/at mk-string reduce [ 0 255 ] 0", "0") && is("s/at mk-string reduce [ 0 255 ] 1", "255"),
          "A4: s/at returns raw bytes, including 0 and 255");
    CHECK(sin_is("s/at 5 0", "type", "s/at") && sin_is("s/at [ 1 ] 0", "type", "s/at"),
          "A5: s/at of a non-string raises 'type 's/at");
    CHECK(sin_is("s/at \"a\" \"0\"", "type", "s/at") && sin_is("s/at \"a\" none", "type", "s/at"),
          "A6: s/at with a non-integer index raises 'type 's/at");

    /* ---- s/slice ---- */
    CHECK(is("s/slice \"hello world\" 6 5", "\"world\"") && is("s/slice \"hello world\" 0 5", "\"hello\""),
          "B1: s/slice takes n bytes from a 0-based start");
    CHECK(is("s/slice \"hello\" 0 5", "\"hello\"") && is("s/= s/slice \"hello\" 0 5 \"hello\"", "1"),
          "B2: s/slice of the whole string is an equal string");
    CHECK(is("s/slice \"hello\" 0 0", "\"\"") && is("s/slice \"hello\" 5 0", "\"\"")
          && is("s/slice \"\" 0 0", "\"\"") && is("s/length s/slice \"hello\" 2 0", "0"),
          "B3: n = 0 gives an empty string, at the start, inside, at the end, and of an empty string");
    CHECK(is("x: \"abc\"  y: s/slice x 0 2  x", "\"abc\"") && is("y", "\"ab\"") && is("= x s/slice x 0 3", "0"),
          "B4: s/slice returns a new string and never changes its input");
    CHECK(sin_is("s/slice \"hello\" 3 3", "range", "s/slice") && sin_is("s/slice \"hello\" 6 0", "range", "s/slice")
          && sin_is("s/slice \"\" 0 1", "range", "s/slice"),
          "B5: a range past the end raises 'range 's/slice (also start beyond the end, and any bytes of an empty string)");
    CHECK(sin_is("s/slice \"hello\" -1 2", "range", "s/slice") && sin_is("s/slice \"hello\" 1 -1", "range", "s/slice"),
          "B6: a negative start or length raises 'range 's/slice");
    CHECK(is("sin-arg judge [ s/slice \"ab\" 1 5 ]", "[ 1 5 ]"), "B7: the 'range SIN!'s arg is [ start n ]");
    CHECK(sin_is("s/slice 7 0 1", "type", "s/slice") && sin_is("s/slice \"ab\" \"0\" 1", "type", "s/slice")
          && sin_is("s/slice \"ab\" 0 none", "type", "s/slice"),
          "B8: a non-string, or a non-integer start or length, raises 'type 's/slice");

    /* ---- s/span: half-open [start, end) ---- */
    CHECK(is("s/span \"hello world\" 6 11", "\"world\"") && is("s/span \"hello world\" 0 5", "\"hello\"")
          && is("s/span \"hello\" 1 4", "\"ell\""),
          "E1: s/span takes the bytes from start up to, not including, end");
    CHECK(is("s/span \"hello\" 0 5", "\"hello\"") && is("s/span \"hello\" 4 5", "\"o\"") && is("s/span \"hello\" 0 1", "\"h\""),
          "E2: s/span of the whole string, the last byte and the first byte");
    CHECK(is("s/span \"hello\" 0 0", "\"\"") && is("s/span \"hello\" 3 3", "\"\"") && is("s/span \"hello\" 5 5", "\"\"")
          && is("s/span \"\" 0 0", "\"\""),
          "E3: start = end gives an empty string, at the start, inside, at the end, and of an empty string");
    {
        /* s/span s a b = s/slice s a (b - a) for every valid a <= b of a 6-byte string */
        int ok = 1;
        char src[160];
        for (int a = 0; a <= 6 && ok; a++)
            for (int b = a; b <= 6 && ok; b++) {
                snprintf(src, sizeof src, "s/= s/span \"abcdef\" %d %d s/slice \"abcdef\" %d %d", a, b, a, b - a);
                if (strcmp(cell_(src), "1") != 0) { printf("    %s gave %s\n", src, text); ok = 0; }
            }
        CHECK(ok, "E4: s/span s a b equals s/slice s a (b - a) for all 28 valid ranges of a 6-byte string");
    }
    CHECK(is("x: \"abc\"  y: s/span x 0 2  x", "\"abc\"") && is("y", "\"ab\"") && is("= x s/span x 0 3", "0"),
          "E5: s/span returns a new string and never changes its input");
    CHECK(sin_is("s/span \"hello\" 0 6", "range", "s/span") && sin_is("s/span \"hello\" 6 6", "range", "s/span")
          && sin_is("s/span \"\" 0 1", "range", "s/span"),
          "E6: an end past the length raises 'range 's/span (also start = end beyond it, and any bytes of an empty string)");
    CHECK(sin_is("s/span \"hello\" -1 2", "range", "s/span") && sin_is("s/span \"hello\" -2 -1", "range", "s/span")
          && sin_is("s/span \"hello\" 3 2", "range", "s/span"),
          "E7: negative indices (no Python-style counting from the end) and end < start raise 'range 's/span");
    CHECK(is("sin-arg judge [ s/span \"ab\" 1 5 ]", "[ 1 5 ]"), "E8: the 'range SIN!'s arg is [ start end ]");
    CHECK(sin_is("s/span 7 0 1", "type", "s/span") && sin_is("s/span \"ab\" \"0\" 1", "type", "s/span")
          && sin_is("s/span \"ab\" 0 none", "type", "s/span"),
          "E9: a non-string, or a non-integer start or end, raises 'type 's/span");

    /* ---- s/scan-set ---- */
    CHECK(is("s/scan-set \"   x\" 0 \" \"", "3") && is("s/scan-set \"abc123\" 0 \"abcdefghijklmnopqrstuvwxyz\"", "3")
          && is("s/scan-set \"abc123\" 3 \"0123456789\"", "6"),
          "C1: s/scan-set stops at the first byte outside the set (or at the length)");
    CHECK(is("s/scan-set \"aaaa\" 0 \"a\"", "4") && is("s/scan-set \"aaaa\" 4 \"a\"", "4") && is("s/scan-set \"abc\" 0 \"x\"", "0"),
          "C2: s/scan-set runs to the end, starts at the end, and stops immediately");
    CHECK(is("s/scan-set \"aaaa\" 9 \"a\"", "4") && is("s/scan-set \"ab\" -3 \"a\"", "1"),
          "C3: s/scan-set clamps i to [0, length]");
    CHECK(is("s/scan-set \"abc\" 1 \"\"", "1") && is("s/scan-set \"\" 0 \"a\"", "0") && is("s/scan-set \"\" 5 \"a\"", "0"),
          "C4: an empty set returns i; an empty string returns 0");
    CHECK(is("s/scan-set \"aab\" 0 \"aaa\"", "2") && is("s/scan-set mk-string reduce [ 0 255 7 ] 0 mk-string reduce [ 255 0 ]", "2"),
          "C5: a set with repeated bytes, and sets containing bytes 0 and 255");
    CHECK(is("t: \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"  "
             "u: s/+ s/+ s/+ s/+ s/+ t t t t t t  s/scan-set s/+ u \"b\" 0 \"a\"", "300"),
          "C6: s/scan-set scans a 300-byte run in one call (no recursion per byte)");
    CHECK(sin_is("s/scan-set \"ab\" 0 5", "type", "s/scan-set") && sin_is("s/scan-set 5 0 \"a\"", "type", "s/scan-set")
          && sin_is("s/scan-set \"ab\" \"0\" \"a\"", "type", "s/scan-set"),
          "C7: a non-string string or set, or a non-integer index, raises 'type 's/scan-set");

    /* ---- s/from-int ---- */
    CHECK(is("s/from-int 0", "\"0\"") && is("s/from-int 7", "\"7\"") && is("s/from-int 10", "\"10\"")
          && is("s/from-int 100", "\"100\"") && is("s/from-int 1000000", "\"1000000\""),
          "D1: s/from-int of zero, digits and powers of ten");
    CHECK(is("s/from-int -1", "\"-1\"") && is("s/from-int -9", "\"-9\"") && is("s/from-int -10", "\"-10\"")
          && is("s/from-int -42", "\"-42\""),
          "D2: s/from-int of negative integers");
    CHECK(is("s/from-int 134217727", "\"134217727\"") && is("s/from-int -134217728", "\"-134217728\""),
          "D3: the 32-bit WebAssembly build's integer extremes (+-2^27)");
    CHECK(is("s/from-int - * 1073741824 536870912 1", "\"576460752303423487\"")
          && is("s/from-int * -1073741824 536870912", "\"-576460752303423488\""),
          "D4: the native build's integer extremes (+-2^59), including the most negative, which has no positive counterpart");
    {
        int ok = 1;
        char src[64], want[64];
        for (long v = -2000; v <= 2000 && ok; v += 37) {
            snprintf(src, sizeof src, "s/from-int %ld", v);
            snprintf(want, sizeof want, "\"%ld\"", v);
            if (strcmp(cell_(src), want) != 0) { printf("    %s gave %s\n", src, text); ok = 0; }
        }
        CHECK(ok, "D5: s/from-int agrees with C printf from -2000 to 2000 (step 37)");
    }
    CHECK(sin_is("s/from-int \"1\"", "type", "s/from-int") && sin_is("s/from-int none", "type", "s/from-int")
          && sin_is("s/from-int [ 1 ]", "type", "s/from-int"),
          "D6: s/from-int of a non-integer raises 'type 's/from-int");

    /* ---- UTF-8: bytes, not characters (documented limitation) ---- */
    CHECK(is("s/length \"\xcf\x80\"", "2") && is("s/at \"\xcf\x80\" 0", "207") && is("s/at \"\xcf\x80\" 1", "128"),
          "U1: \"pi\" (U+03C0) is two bytes, 207 128; s/at returns each byte");
    CHECK(is("s/length s/slice \"\xcf\x80\" 0 1", "1") && is("s/at s/slice \"\xcf\x80\" 0 1 0", "207"),
          "U2: s/slice can split a multi-byte character (the result is not valid UTF-8)");
    CHECK(is("s/= s/slice \"a\xcf\x80" "b\" 1 2 \"\xcf\x80\"", "1"),
          "U3: slicing on byte boundaries keeps the character intact");
    CHECK(is("s/scan-set \"\xcf\x80\xcf\x80x\" 0 \"\xcf\x80\"", "4") && is("s/scan-set \"\xcf\x80" "a\" 0 \"a\"", "0")
          && is("s/scan-set \"\xcf\xcf\x80\" 0 \"\xcf\x80\"", "3"),
          "U4: an s/scan-set set is a set of BYTES: it matches the bytes of a character individually");
    CHECK(is("s/length s/span \"a\xcf\x80\" 0 2", "2") && is("s/at s/span \"a\xcf\x80\" 0 2 1", "207")
          && is("s/= s/span \"a\xcf\x80" "b\" 1 3 \"\xcf\x80\"", "1"),
          "U6: s/span, like s/slice, can split a multi-byte character; byte boundaries keep it intact");
    CHECK(is("s/+ s/from-int 3 \" \xe2\x82\xac\"", "\"3 \xe2\x82\xac\"") && is("s/length \" \xe2\x82\xac\"", "4"),
          "U5: UTF-8 text passes through unchanged (a 3-byte character counts as 3)");

    return failures;
}
