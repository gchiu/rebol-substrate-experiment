/* tests/core-c3/deps_probe.c - GLON-C3-1 server-stack dependency probe
 * (make server-deps-check).
 *
 * Exercises, on the host toolchain, exactly the third-party APIs the Glon
 * Worlds Native server modules call (W5.1 0af12b2):
 *   glon_sec.c     OpenSSL 3: RAND_bytes, EVP_Digest + EVP_sha256,
 *                  EVP_PBE_scrypt, CRYPTO_memcmp
 *   glon_sqlite.c  SQLite 3: open_v2, busy_timeout, exec, prepare_v2,
 *                  bind_{int64,text,blob,null}, bind_parameter_count, step,
 *                  column_{count,type,int64,double,text,blob,bytes}, changes,
 *                  reset, clear_bindings, finalize, errmsg, free, close_v2
 *   glon_http.c    pthread mutex/cond with cond_timedwait on CLOCK_REALTIME,
 *                  clock_gettime(CLOCK_MONOTONIC)
 * so a missing or incompatible dependency is found on each CI platform before
 * the modules themselves are ported.  It links no Glon code and changes no
 * Glon behaviour.  Prints one line per check and DEPS_PROBE PASS / FAIL.
 */
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/opensslv.h>
#include <sqlite3.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails;
static void check(int ok, const char *what) {
    printf("  %s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) fails++;
}

static void probe_openssl(void) {
    unsigned char r[32];
    check(RAND_bytes(r, (int)sizeof r) == 1, "RAND_bytes");

    /* SHA-256("abc") */
    static const unsigned char want[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad };
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdlen = 0;
    int ok = EVP_Digest("abc", 3, md, &mdlen, EVP_sha256(), NULL) == 1;
    check(ok && mdlen == 32 && memcmp(md, want, 32) == 0, "EVP_Digest(EVP_sha256) known answer");

    /* RFC 7914 scrypt test vector 2: P="password", S="NaCl", N=1024, r=8, p=16 */
    static const unsigned char want_s[16] = {
        0xfd,0xba,0xbe,0x1c,0x9d,0x34,0x72,0x00,0x78,0x56,0xe7,0x19,0x0d,0x01,0xe9,0xfe };
    unsigned char key[64];
    ok = EVP_PBE_scrypt("password", 8, (const unsigned char *)"NaCl", 4,
                        1024, 8, 16, 64ull * 1024 * 1024, key, sizeof key) == 1;
    check(ok && memcmp(key, want_s, 16) == 0, "EVP_PBE_scrypt RFC 7914 vector");

    check(CRYPTO_memcmp("same", "same", 4) == 0 && CRYPTO_memcmp("same", "sane", 4) != 0,
          "CRYPTO_memcmp");
    printf("  info: %s\n", OPENSSL_VERSION_TEXT);
}

static void probe_sqlite(void) {
    sqlite3 *db = NULL;
    int ok = sqlite3_open_v2(":memory:", &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK;
    check(ok, "sqlite3_open_v2(:memory:)");
    if (!ok) { if (db) sqlite3_close_v2(db); return; }
    check(sqlite3_busy_timeout(db, 1000) == SQLITE_OK, "sqlite3_busy_timeout");
    char *err = NULL;
    ok = sqlite3_exec(db, "BEGIN; CREATE TABLE t(i INTEGER, r REAL, s TEXT, b BLOB, n); COMMIT;",
                      NULL, NULL, &err) == SQLITE_OK;
    check(ok, "sqlite3_exec (transaction, DDL)");
    if (err) { printf("    %s\n", err); sqlite3_free(err); }

    sqlite3_stmt *st = NULL;
    ok = sqlite3_prepare_v2(db, "INSERT INTO t VALUES (?1, 2.5, ?2, ?3, ?4)", -1, &st, NULL) == SQLITE_OK;
    check(ok && sqlite3_bind_parameter_count(st) == 4, "sqlite3_prepare_v2 + bind_parameter_count");
    if (ok) {
        sqlite3_bind_int64(st, 1, (sqlite3_int64)1 << 40);
        sqlite3_bind_text(st, 2, "glon", -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 3, "\0\1\2", 3, SQLITE_TRANSIENT);
        sqlite3_bind_null(st, 4);
        check(sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) == 1, "bind + step + changes");
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
        sqlite3_finalize(st);
    }
    ok = sqlite3_prepare_v2(db, "SELECT i, r, s, b, n FROM t", -1, &st, NULL) == SQLITE_OK
         && sqlite3_step(st) == SQLITE_ROW && sqlite3_column_count(st) == 5;
    check(ok, "select + column_count");
    if (ok) {
        check(sqlite3_column_type(st, 0) == SQLITE_INTEGER
              && sqlite3_column_int64(st, 0) == ((sqlite3_int64)1 << 40), "column_int64");
        check(sqlite3_column_double(st, 1) == 2.5, "column_double");
        check(strcmp((const char *)sqlite3_column_text(st, 2), "glon") == 0, "column_text");
        check(sqlite3_column_bytes(st, 3) == 3
              && memcmp(sqlite3_column_blob(st, 3), "\0\1\2", 3) == 0, "column_blob + bytes");
        check(sqlite3_column_type(st, 4) == SQLITE_NULL, "column_type NULL");
    }
    sqlite3_finalize(st);
    check(sqlite3_prepare_v2(db, "SELECT nope FROM t", -1, &st, NULL) != SQLITE_OK
          && strlen(sqlite3_errmsg(db)) > 0, "errmsg on a failed prepare");
    check(sqlite3_close_v2(db) == SQLITE_OK, "sqlite3_close_v2");
    printf("  info: SQLite %s\n", sqlite3_libversion());
}

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv;
static int signalled;
static void *signaller(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mu);
    signalled = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    return NULL;
}
static void probe_threads(void) {
    struct timespec mono;
    check(clock_gettime(CLOCK_MONOTONIC, &mono) == 0, "clock_gettime(CLOCK_MONOTONIC)");
    check(pthread_cond_init(&cv, NULL) == 0, "pthread_cond_init");
    /* a timed wait that times out, then one that is signalled */
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_nsec += 20 * 1000 * 1000;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&mu);
    int rc = pthread_cond_timedwait(&cv, &mu, &dl);
    pthread_mutex_unlock(&mu);
    check(rc != 0, "pthread_cond_timedwait times out (CLOCK_REALTIME deadline)");
    pthread_t th;
    pthread_mutex_lock(&mu);
    int ok = pthread_create(&th, NULL, signaller, NULL) == 0;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 10;
    rc = 0;
    while (ok && !signalled && rc != ETIMEDOUT)
        rc = pthread_cond_timedwait(&cv, &mu, &dl);
    pthread_mutex_unlock(&mu);
    if (ok) pthread_join(th, NULL);
    check(ok && signalled, "pthread_create + cond_broadcast wakes a timed waiter");
    check(pthread_cond_destroy(&cv) == 0 && pthread_mutex_destroy(&mu) == 0,
          "pthread_cond_destroy + mutex_destroy");
}

int main(void) {
    printf("deps-probe: OpenSSL\n");
    probe_openssl();
    printf("deps-probe: SQLite\n");
    probe_sqlite();
    printf("deps-probe: threads/clocks\n");
    probe_threads();
    if (fails) { printf("DEPS_PROBE FAIL (%d)\n", fails); return 1; }
    printf("DEPS_PROBE PASS\n");
    return 0;
}
