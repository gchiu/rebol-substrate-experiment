/* glon_sec.h - optional general-purpose security primitives for the Glon CLI.
 *
 * This module supplies CSPRNG bytes, SHA-256, scrypt, constant-time equality
 * and wall-clock seconds through the host request/reply boundary.  It is
 * compile-time optional (MODULES=sec) and contains no application, user,
 * session, chat or authentication policy.
 *
 * All values crossing the boundary are bounded.  Secrets are copied into this
 * fixed request buffer while the host operation runs and cleansed afterward;
 * the managed Glon string holding a secret is not wiped by this module.
 */
#ifndef GLON_SEC_H
#define GLON_SEC_H

#include <stddef.h>

#define GLON_SEC_MAX_FIELDS 6
#define GLON_SEC_FIELD_CAP 1024
#define GLON_SEC_OUT_CAP 64
#define GLON_SEC_MAX_INPUT 1024

enum {
    GLON_SEC_OP_RANDOM = 1,
    GLON_SEC_OP_SHA256 = 2,
    GLON_SEC_OP_SCRYPT = 3,
    GLON_SEC_OP_CT_EQ = 4,
    GLON_SEC_OP_TIME = 5
};

typedef struct {
    unsigned op;
    unsigned nfields;
    unsigned char fields[GLON_SEC_MAX_FIELDS][GLON_SEC_FIELD_CAP];
    size_t field_len[GLON_SEC_MAX_FIELDS];
} glon_sec_request;

/* Execute one already-validated request.  Outputs are raw bytes; out2 is
 * currently always empty and exists so the host reply keeps one shape.
 * Returns 0 on success; -1 with a human-readable err on invalid input or an
 * OpenSSL failure.  Never logs secret bytes. */
int glon_sec_execute(const glon_sec_request *req,
                     unsigned char *out1, size_t *out1_len,
                     unsigned char *out2, size_t *out2_len,
                     char *err, size_t errcap);

#endif /* GLON_SEC_H */
