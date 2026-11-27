/* sqlite_tests.c - focused tests for the optional SQLite host service. */
#include "glon_sqlite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static void field(glon_sqlite_request *r, unsigned i, const void *p, size_t n) {
    memcpy(r->fields[i], p, n);
    r->field_len[i] = n;
}
static void text(glon_sqlite_request *r, unsigned i, const char *s) {
    field(r, i, s, strlen(s));
}
static void req1(glon_sqlite_request *r, unsigned op, const char *a) {
    memset(r, 0, sizeof *r); r->op = op; r->nfields = 1; text(r, 0, a);
}
static void req2(glon_sqlite_request *r, unsigned op, const char *a, const char *b) {
    memset(r, 0, sizeof *r); r->op = op; r->nfields = 2; text(r, 0, a); text(r, 1, b);
}
static void req3(glon_sqlite_request *r, unsigned op, const char *a, const char *b,
                 const void *c, size_t cn) {
    memset(r, 0, sizeof *r); r->op = op; r->nfields = 3;
    text(r, 0, a); text(r, 1, b); field(r, 2, c, cn);
}
static int run(glon_sqlite_service *s, glon_sqlite_request *r,
               glon_sqlite_result *out) {
    char err[256];
    return glon_sqlite_execute(s, r, out, err, sizeof err);
}
static unsigned cell_u32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | (unsigned)p[3];
}
static long row_int(const glon_sqlite_result *r, unsigned col) {
    if (r->len < 1 || r->data[0] <= col) return -999999;
    size_t pos = 1;
    for (unsigned i = 0; i <= col; i++) {
        unsigned type = r->data[pos++];
        if (i == col) {
            if (type != 1) return -999999;
            long v = 0;
            for (int k = 0; k < 8; k++) v = (v << 8) | r->data[pos + (size_t)k];
            return v;
        }
        if (type == 0) continue;
        if (type == 1) pos += 8;
        else { unsigned n = cell_u32(r->data + pos); pos += 4 + n; }
    }
    return -999999;
}
static const unsigned char *row_data(const glon_sqlite_result *r, unsigned col,
                                     unsigned *type, unsigned *len) {
    if (r->len < 1 || r->data[0] <= col) return NULL;
    size_t pos = 1;
    for (unsigned i = 0; i <= col; i++) {
        unsigned t = r->data[pos++];
        if (t == 0) { if (i == col) { *type = t; *len = 0; return NULL; } continue; }
        if (t == 1) {
            if (i == col) { *type = t; *len = 8; return r->data + pos; }
            pos += 8; continue;
        }
        unsigned n = cell_u32(r->data + pos); pos += 4;
        if (i == col) { *type = t; *len = n; return r->data + pos; }
        pos += n;
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: sqlite_tests DIR\n"); return 2; }
    glon_sqlite_config cfg;
    glon_sqlite_config_default(&cfg);
    cfg.root = argv[1];
    cfg.allow_read = 1;
    cfg.allow_write = 1;
    glon_sqlite_service svc;
    char err[256];
    if (glon_sqlite_init(&svc, &cfg, err, sizeof err) != 0) {
        fprintf(stderr, "init: %s\n", err); return 2;
    }
    printf("G3D SQLite service\n");
    glon_sqlite_request q;
    glon_sqlite_result r;
    unsigned char db[17], st[17], db2[17];

    req1(&q, GLON_SQLITE_OP_OPEN, "test.db");
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_OK && r.len == 16,
          "open creates a database and returns an opaque handle");
    memcpy(db, r.data, 16); db[16] = 0;

    req1(&q, GLON_SQLITE_OP_OPEN, "../escape.db");
    CHECK(run(&svc, &q, &r) != 0 &&
          (r.status == GLON_SQLITE_PERMISSION || r.status == GLON_SQLITE_NOT_FOUND),
          "sandbox rejects ../ database paths");

    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db,
         "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, n INTEGER, b BLOB)");
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_OK,
          "exec creates a table");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db,
         "INSERT INTO t(name,n,b) VALUES ('alpha',1,x'00ff')");
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_OK, "exec inserts a row");

    req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)db,
         "SELECT id,name,n,b FROM t WHERE id=?");
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_OK && r.len == 16,
          "prepare returns a statement handle");
    memcpy(st, r.data, 16); st[16] = 0;

    req3(&q, GLON_SQLITE_OP_BIND_INT, (const char *)st, "1", "1", 1);
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_OK, "bind integer parameter");
    req1(&q, GLON_SQLITE_OP_STEP, (const char *)st);
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_ROW && row_int(&r, 0) == 1,
          "step returns a typed row");
    unsigned type = 0, len = 0;
    const unsigned char *v = row_data(&r, 1, &type, &len);
    CHECK(v && type == 2 && len == 5 && memcmp(v, "alpha", 5) == 0,
          "text column round trips");
    CHECK(row_int(&r, 2) == 1, "integer column round trips");
    v = row_data(&r, 3, &type, &len);
    CHECK(v && type == 3 && len == 2 && v[0] == 0 && v[1] == 0xff,
          "blob column round trips");
    req1(&q, GLON_SQLITE_OP_STEP, (const char *)st);
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_DONE,
          "step distinguishes end-of-results from a row");

    req1(&q, GLON_SQLITE_OP_RESET, (const char *)st);
    CHECK(run(&svc, &q, &r) == 0, "reset allows statement reuse");
    req3(&q, GLON_SQLITE_OP_BIND_INT, (const char *)st, "1", "1", 1);
    CHECK(run(&svc, &q, &r) == 0, "rebind after reset");
    req1(&q, GLON_SQLITE_OP_CLEAR, (const char *)st);
    CHECK(run(&svc, &q, &r) == 0, "clear bindings");

    /* Bind a hostile value; it must remain data, not SQL. */
    req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)db,
         "INSERT INTO t(name,n,b) VALUES (?,?,?)");
    unsigned char ins[17];
    CHECK(run(&svc, &q, &r) == 0, "prepare insert");
    memcpy(ins, r.data, 16); ins[16] = 0;
    const char *evil = "x' OR 1=1 --";
    req3(&q, GLON_SQLITE_OP_BIND_TEXT, (const char *)ins, "1", evil, strlen(evil));
    CHECK(run(&svc, &q, &r) == 0, "bind hostile string");
    req3(&q, GLON_SQLITE_OP_BIND_INT, (const char *)ins, "2", "7", 1);
    CHECK(run(&svc, &q, &r) == 0, "bind second parameter");
    req1(&q, GLON_SQLITE_OP_STEP, (const char *)ins);
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_DONE,
          "binding does not execute injected SQL");

    req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)db, "SELEC nope");
    CHECK(run(&svc, &q, &r) != 0 && r.status == GLON_SQLITE_ERROR,
          "malformed SQL is a recoverable prepare error");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db,
         "INSERT INTO t(name,n,b) VALUES ('nullrow',6,NULL)");
    CHECK(run(&svc, &q, &r) == 0, "insert SQL NULL row");
    req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)db,
         "SELECT b FROM t WHERE name='nullrow'");
    CHECK(run(&svc, &q, &r) == 0, "prepare NULL query");
    unsigned char nst[17]; memcpy(nst, r.data, 16); nst[16] = 0;
    req1(&q, GLON_SQLITE_OP_STEP, (const char *)nst);
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_ROW &&
          row_data(&r, 0, &type, &len) == NULL && type == 0,
          "SQL NULL is distinct from an empty string");
    req1(&q, GLON_SQLITE_OP_FINALIZE, (const char *)nst);
    (void)run(&svc, &q, &r);

    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db,
         "INSERT INTO t(name,n,b) VALUES (printf('%.*c', 9000, 'x'),7,NULL)");
    CHECK(run(&svc, &q, &r) == 0, "insert oversized row");
    req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)db,
         "SELECT name FROM t WHERE n=7 ORDER BY length(name) DESC");
    CHECK(run(&svc, &q, &r) == 0, "prepare oversized row query");
    unsigned char lrg[17]; memcpy(lrg, r.data, 16); lrg[16] = 0;
    req1(&q, GLON_SQLITE_OP_STEP, (const char *)lrg);
    CHECK(run(&svc, &q, &r) != 0 && r.status == GLON_SQLITE_TOO_LARGE,
          "oversized row is rejected instead of materialised");
    req1(&q, GLON_SQLITE_OP_FINALIZE, (const char *)lrg);
    (void)run(&svc, &q, &r);

    /* Constraint error */
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db,
         "INSERT INTO t(name,n,b) VALUES ('alpha',2,x'')");
    CHECK(run(&svc, &q, &r) != 0 && r.status == GLON_SQLITE_CONSTRAINT,
          "constraint violation is a recoverable SQLite error");

    /* Transaction rollback */
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db, "BEGIN");
    CHECK(run(&svc, &q, &r) == 0, "BEGIN");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db,
         "INSERT INTO t(name,n,b) VALUES ('rolled',3,x'')");
    CHECK(run(&svc, &q, &r) == 0, "insert inside transaction");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db, "ROLLBACK");
    CHECK(run(&svc, &q, &r) == 0, "ROLLBACK");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db,
         "INSERT INTO t(name,n,b) VALUES ('kept',4,x'')");
    CHECK(run(&svc, &q, &r) == 0, "insert committed row");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db, "BEGIN");
    CHECK(run(&svc, &q, &r) == 0, "BEGIN for commit");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db, "COMMIT");
    CHECK(run(&svc, &q, &r) == 0, "COMMIT");

    /* Second connection holds an exclusive transaction: serialised single-owner
     * host still reports SQLite busy rather than blocking or corrupting. */
    req1(&q, GLON_SQLITE_OP_OPEN, "test.db");
    CHECK(run(&svc, &q, &r) == 0, "second database handle");
    memcpy(db2, r.data, 16); db2[16] = 0;
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db2, "BEGIN EXCLUSIVE");
    CHECK(run(&svc, &q, &r) == 0, "second connection takes exclusive lock");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db,
         "INSERT INTO t(name,n,b) VALUES ('busy',5,x'')");
    CHECK(run(&svc, &q, &r) != 0 && r.status == GLON_SQLITE_BUSY,
          "locked database reports busy");
    req2(&q, GLON_SQLITE_OP_EXEC, (const char *)db2, "ROLLBACK");
    CHECK(run(&svc, &q, &r) == 0, "release exclusive lock");

    /* Finalize/close invalidates handles. */
    req1(&q, GLON_SQLITE_OP_FINALIZE, (const char *)ins);
    CHECK(run(&svc, &q, &r) == 0, "finalize statement");
    req1(&q, GLON_SQLITE_OP_STEP, (const char *)ins);
    CHECK(run(&svc, &q, &r) != 0 &&
          (r.status == GLON_SQLITE_INVALID_HANDLE || r.status == GLON_SQLITE_STALE_HANDLE),
          "use-after-finalize is rejected");
    req1(&q, GLON_SQLITE_OP_CLOSE, (const char *)db2);
    CHECK(run(&svc, &q, &r) == 0, "close second database");
    req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)db2, "SELECT 1");
    CHECK(run(&svc, &q, &r) != 0 &&
          (r.status == GLON_SQLITE_INVALID_HANDLE || r.status == GLON_SQLITE_STALE_HANDLE),
          "use-after-close is rejected");

    /* Oversized value is rejected without materialising a result. */
    char big[GLON_SQLITE_MAX_VALUE + 2];
    memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0;
    req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)db, "SELECT ?");
    CHECK(run(&svc, &q, &r) == 0, "prepare oversized-value statement");
    unsigned char bigst[17]; memcpy(bigst, r.data, 16); bigst[16] = 0;
    req3(&q, GLON_SQLITE_OP_BIND_TEXT, (const char *)bigst, "1", big, sizeof big - 1);
    CHECK(run(&svc, &q, &r) != 0 && r.status == GLON_SQLITE_TOO_LARGE,
          "oversized binding is rejected");
    req1(&q, GLON_SQLITE_OP_FINALIZE, (const char *)bigst);
    CHECK(run(&svc, &q, &r) == 0, "finalize oversized-value statement");

    req1(&q, GLON_SQLITE_OP_FINALIZE, (const char *)st);
    CHECK(run(&svc, &q, &r) == 0, "finalize select statement");
    req1(&q, GLON_SQLITE_OP_CLOSE, (const char *)db);
    CHECK(run(&svc, &q, &r) == 0, "close database");

    /* Persistence across close/reopen in the same process (and the example
     * extends this across process restart). */
    req1(&q, GLON_SQLITE_OP_OPEN, "test.db");
    CHECK(run(&svc, &q, &r) == 0, "reopen database");
    memcpy(db, r.data, 16); db[16] = 0;
    req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)db, "SELECT count(*) FROM t");
    CHECK(run(&svc, &q, &r) == 0, "prepare count");
    memcpy(st, r.data, 16); st[16] = 0;
    req1(&q, GLON_SQLITE_OP_STEP, (const char *)st);
    CHECK(run(&svc, &q, &r) == 0 && r.status == GLON_SQLITE_ROW &&
          row_int(&r, 0) == 5, "committed rows persist");
    req1(&q, GLON_SQLITE_OP_FINALIZE, (const char *)st);
    (void)run(&svc, &q, &r);
    req1(&q, GLON_SQLITE_OP_CLOSE, (const char *)db);
    (void)run(&svc, &q, &r);

    unsigned dbs = 0, stmts = 0;
    glon_sqlite_destroy(&svc, &dbs, &stmts);
    CHECK(dbs == 0 && stmts == 0, "cleanup leaves no open resources");

    /* Shutdown with outstanding database and statement resources. */
    glon_sqlite_service svc2;
    if (glon_sqlite_init(&svc2, &cfg, err, sizeof err) == 0) {
        req1(&q, GLON_SQLITE_OP_OPEN, "outstanding.db");
        CHECK(run(&svc2, &q, &r) == 0, "open database for shutdown test");
        unsigned char odb[17]; memcpy(odb, r.data, 16); odb[16] = 0;
        req2(&q, GLON_SQLITE_OP_PREPARE, (const char *)odb, "SELECT 1");
        CHECK(run(&svc2, &q, &r) == 0, "leave statement outstanding");
        unsigned d2 = 0, s2 = 0;
        glon_sqlite_destroy(&svc2, &d2, &s2);
        CHECK(d2 == 1 && s2 == 1, "shutdown finalizes statements before closing databases");
    } else {
        CHECK(0, "init shutdown service");
    }

    if (failures == 0) printf("all SQLite service tests passed\n");
    return failures ? 1 : 0;
}
