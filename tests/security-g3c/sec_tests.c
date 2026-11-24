/* sec_tests.c - focused tests for the optional general-purpose security host. */
#include "glon_sec.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static void field(glon_sec_request *r, unsigned i, const void *p, size_t n) {
    memcpy(r->fields[i], p, n);
    r->field_len[i] = n;
}

static void field_text(glon_sec_request *r, unsigned i, const char *s) {
    field(r, i, s, strlen(s));
}

static int hexbyte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int hexeq(const unsigned char *p, size_t n, const char *hex) {
    if (strlen(hex) != n * 2) return 0;
    for (size_t i = 0; i < n; i++) {
        int a = hexbyte(hex[i * 2]), b = hexbyte(hex[i * 2 + 1]);
        if (a < 0 || b < 0 || p[i] != (unsigned char)(a * 16 + b)) return 0;
    }
    return 1;
}

static unsigned long long be64(const unsigned char *p) {
    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

int main(void) {
    unsigned char out1[64], out2[64];
    size_t n1, n2;
    char err[128];
    glon_sec_request r;

    printf("G3C security primitives\n");

    memset(&r, 0, sizeof r);
    r.op = GLON_SEC_OP_SHA256;
    r.nfields = 1;
    field_text(&r, 0, "abc");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == 0 &&
          n1 == 32 && n2 == 0 &&
          hexeq(out1, n1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "SHA-256(abc) matches the standard vector");

    memset(&r, 0, sizeof r);
    r.op = GLON_SEC_OP_SCRYPT;
    r.nfields = 6;
    field_text(&r, 0, "correct horse battery staple");
    field_text(&r, 1, "0123456789abcdef");
    field_text(&r, 2, "16384");
    field_text(&r, 3, "8");
    field_text(&r, 4, "1");
    field_text(&r, 5, "32");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == 0 &&
          n1 == 32 && n2 == 0 &&
          hexeq(out1, n1, "b632b4ded46f123a8270fc2682d75d324823957ae4f14fdbf6b22f7de04c2827"),
          "scrypt matches Python hashlib (N=16384 r=8 p=1)");

    /* Wrong password produces a different derived key; constant-time compare
     * then rejects it.  This is the primitive-level password rejection path. */
    memset(&r, 0, sizeof r);
    r.op = GLON_SEC_OP_SCRYPT;
    r.nfields = 6;
    field_text(&r, 0, "wrong password");
    field_text(&r, 1, "0123456789abcdef");
    field_text(&r, 2, "16384");
    field_text(&r, 3, "8");
    field_text(&r, 4, "1");
    field_text(&r, 5, "32");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == 0 &&
          n1 == 32 &&
          !hexeq(out1, n1, "b632b4ded46f123a8270fc2682d75d324823957ae4f14fdbf6b22f7de04c2827"),
          "wrong password derives a different scrypt key");

    memset(&r, 0, sizeof r);
    r.op = GLON_SEC_OP_CT_EQ;
    r.nfields = 2;
    field_text(&r, 0, "same");
    field_text(&r, 1, "same");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == 0 &&
          n1 == 8 && be64(out1) == 1, "ct-eq accepts equal bytes");
    field_text(&r, 1, "diff");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == 0 &&
          be64(out1) == 0, "ct-eq rejects different bytes");
    field_text(&r, 1, "same!");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == 0 &&
          be64(out1) == 0, "ct-eq rejects different lengths");

    memset(&r, 0, sizeof r);
    r.op = GLON_SEC_OP_TIME;
    r.nfields = 0;
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == 0 &&
          n1 == 8 && be64(out1) > 1700000000ULL, "wall clock returns epoch seconds");

    int unique = 0;
    unsigned char first[16];
    memset(first, 0, sizeof first);
    for (int i = 0; i < 16; i++) {
        memset(&r, 0, sizeof r);
        r.op = GLON_SEC_OP_RANDOM;
        r.nfields = 1;
        field_text(&r, 0, "16");
        if (glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) != 0 ||
            n1 != 16) { unique = -1; break; }
        if (i == 0 || memcmp(first, out1, 16) != 0) unique = 1;
    }
    CHECK(unique == 1, "random tokens are non-repeating at 16 bytes");

    memset(&r, 0, sizeof r);
    r.op = GLON_SEC_OP_RANDOM;
    r.nfields = 1;
    field_text(&r, 0, "0");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == -1,
          "zero-length random is rejected");
    field_text(&r, 0, "65");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == -1,
          "oversized random is rejected");

    memset(&r, 0, sizeof r);
    r.op = GLON_SEC_OP_SCRYPT;
    r.nfields = 6;
    field_text(&r, 0, "pw");
    field_text(&r, 1, "salt");
    field_text(&r, 2, "999");
    field_text(&r, 3, "8");
    field_text(&r, 4, "1");
    field_text(&r, 5, "32");
    CHECK(glon_sec_execute(&r, out1, &n1, out2, &n2, err, sizeof err) == -1,
          "invalid scrypt N is rejected");

    if (failures == 0) printf("all security primitive tests passed\n");
    return failures ? 1 : 0;
}
