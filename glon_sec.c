/* glon_sec.c - optional security primitives backed by OpenSSL.
 *
 * API/ownership are deliberately small and general: random bytes, SHA-256,
 * scrypt, constant-time comparison, and Unix epoch seconds.  Authentication,
 * account, session, CSRF and chat policy belong in Glon application code.
 */
#include "glon_sec.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int parse_u32(const unsigned char *p, size_t n, unsigned *out) {
    if (n == 0 || n > 10) return -1;
    unsigned long v = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return -1;
        v = v * 10u + (unsigned long)(p[i] - '0');
        if (v > 0xfffffffful) return -1;
    }
    *out = (unsigned)v;
    return 0;
}

static void put_be64(unsigned char *out, uint64_t v) {
    for (int i = 7; i >= 0; i--) {
        out[i] = (unsigned char)(v & 0xffu);
        v >>= 8;
    }
}

static int fail(char *err, size_t errcap, const char *message) {
    if (errcap) snprintf(err, errcap, "%s", message);
    return -1;
}

int glon_sec_execute(const glon_sec_request *req,
                     unsigned char *out1, size_t *out1_len,
                     unsigned char *out2, size_t *out2_len,
                     char *err, size_t errcap) {
    if (!req || !out1 || !out1_len || !out2 || !out2_len)
        return fail(err, errcap, "invalid request");
    *out1_len = 0;
    *out2_len = 0;

    switch (req->op) {
    case GLON_SEC_OP_RANDOM: {
        unsigned n;
        if (req->nfields != 1 ||
            parse_u32(req->fields[0], req->field_len[0], &n) != 0 ||
            n < 1 || n > GLON_SEC_OUT_CAP)
            return fail(err, errcap, "random length must be 1..64");
        if (RAND_bytes(out1, (int)n) != 1)
            return fail(err, errcap, "secure random generation failed");
        *out1_len = n;
        return 0;
    }
    case GLON_SEC_OP_SHA256: {
        if (req->nfields != 1 || req->field_len[0] > GLON_SEC_MAX_INPUT)
            return fail(err, errcap, "sha256 input must be <=1024 bytes");
        unsigned int outlen = 0;
        if (EVP_Digest(req->fields[0], req->field_len[0], out1, &outlen,
                       EVP_sha256(), NULL) != 1 || outlen != 32)
            return fail(err, errcap, "sha256 failed");
        *out1_len = 32;
        return 0;
    }
    case GLON_SEC_OP_SCRYPT: {
        unsigned n = 0, r = 0, p = 0, dklen = 0;
        if (req->nfields != 6 ||
            parse_u32(req->fields[2], req->field_len[2], &n) != 0 ||
            parse_u32(req->fields[3], req->field_len[3], &r) != 0 ||
            parse_u32(req->fields[4], req->field_len[4], &p) != 0 ||
            parse_u32(req->fields[5], req->field_len[5], &dklen) != 0)
            return fail(err, errcap, "invalid scrypt parameters");
        if (req->field_len[0] > GLON_SEC_MAX_INPUT ||
            req->field_len[1] < 1 || req->field_len[1] > 64)
            return fail(err, errcap, "scrypt password/salt too large");
        if (n < 1024u || n > 65536u || (n & (n - 1u)) != 0 ||
            r < 1u || r > 16u || p < 1u || p > 4u ||
            dklen < 16u || dklen > GLON_SEC_OUT_CAP)
            return fail(err, errcap, "scrypt parameters out of range");
        /* 64 MiB is a bounded host-side ceiling, not a Glon heap allocation. */
        if (EVP_PBE_scrypt((const char *)req->fields[0], req->field_len[0],
                           req->fields[1], req->field_len[1],
                           (uint64_t)n, (uint64_t)r, (uint64_t)p,
                           64u * 1024u * 1024u, out1, dklen) != 1)
            return fail(err, errcap, "scrypt failed");
        *out1_len = dklen;
        return 0;
    }
    case GLON_SEC_OP_CT_EQ: {
        if (req->nfields != 2 ||
            req->field_len[0] > GLON_SEC_MAX_INPUT ||
            req->field_len[1] > GLON_SEC_MAX_INPUT)
            return fail(err, errcap, "ct-eq input must be <=1024 bytes");
        unsigned equal = req->field_len[0] == req->field_len[1]
            ? CRYPTO_memcmp(req->fields[0], req->fields[1], req->field_len[0]) == 0
            : 0;
        put_be64(out1, equal ? 1u : 0u);
        *out1_len = 8;
        return 0;
    }
    case GLON_SEC_OP_TIME: {
        if (req->nfields != 0)
            return fail(err, errcap, "time takes no fields");
        time_t now = time(NULL);
        if (now == (time_t)-1)
            return fail(err, errcap, "wall clock unavailable");
        put_be64(out1, (uint64_t)now);
        *out1_len = 8;
        return 0;
    }
    default:
        return fail(err, errcap, "unknown security operation");
    }
}
