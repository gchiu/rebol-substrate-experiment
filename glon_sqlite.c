/* glon_sqlite.c - optional bounded SQLite service for the Glon CLI.
 *
 * Single-owner design: all calls run on the host evaluator thread and SQLite
 * is never entered concurrently.  Statements and databases are opaque slots
 * with generations; a stale/finalized handle cannot reach a native pointer.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "glon_sqlite.h"
#include "glon_io_file.h"

#include <sqlite3.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

void glon_sqlite_config_default(glon_sqlite_config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->root = ".";
    cfg->max_dbs = GLON_SQLITE_MAX_DBS;
    cfg->max_stmts = GLON_SQLITE_MAX_STMTS;
    cfg->max_sql = GLON_SQLITE_MAX_SQL;
    cfg->max_value = GLON_SQLITE_MAX_VALUE;
    cfg->max_row = GLON_SQLITE_MAX_ROW;
}

const char *glon_sqlite_status_word(int status) {
    switch (status) {
    case GLON_SQLITE_OK: return "ok";
    case GLON_SQLITE_ROW: return "row";
    case GLON_SQLITE_DONE: return "done";
    case GLON_SQLITE_BUSY: return "busy";
    case GLON_SQLITE_CONSTRAINT: return "constraint";
    case GLON_SQLITE_MALFORMED: return "malformed";
    case GLON_SQLITE_INVALID_HANDLE: return "invalid-handle";
    case GLON_SQLITE_STALE_HANDLE: return "stale-handle";
    case GLON_SQLITE_TOO_LARGE: return "too-large";
    case GLON_SQLITE_PERMISSION: return "permission";
    case GLON_SQLITE_NOT_FOUND: return "not-found";
    case GLON_SQLITE_OOM: return "out-of-memory";
    case GLON_SQLITE_READONLY: return "read-only";
    case GLON_SQLITE_ERROR: return "error";
    default: return "error";
    }
}

static void set_error(glon_sqlite_result *r, int status, const char *msg) {
    r->status = status;
    r->len = 0;
    if (!msg) msg = "";
    snprintf(r->error, sizeof r->error, "%s", msg);
}

static int sqlite_status(int rc) {
    switch (rc & 0xff) {
    case SQLITE_BUSY:
    case SQLITE_LOCKED: return GLON_SQLITE_BUSY;
    case SQLITE_CONSTRAINT: return GLON_SQLITE_CONSTRAINT;
    case SQLITE_MISUSE:
    case SQLITE_RANGE:
    case SQLITE_FORMAT: return GLON_SQLITE_MALFORMED;
    case SQLITE_NOMEM: return GLON_SQLITE_OOM;
    case SQLITE_READONLY: return GLON_SQLITE_READONLY;
    case SQLITE_CANTOPEN:
    case SQLITE_NOTFOUND: return GLON_SQLITE_NOT_FOUND;
    case SQLITE_PERM:
    case SQLITE_AUTH: return GLON_SQLITE_PERMISSION;
    case SQLITE_TOOBIG: return GLON_SQLITE_TOO_LARGE;
    default: return GLON_SQLITE_ERROR;
    }
}

static void set_sqlite_error(glon_sqlite_result *r, sqlite3 *db, int rc,
                             const char *fallback) {
    const char *msg = db ? sqlite3_errmsg(db) : fallback;
    set_error(r, sqlite_status(rc), msg ? msg : fallback);
}

static uint32_t next_generation(glon_sqlite_service *svc) {
    svc->next_generation++;
    if (svc->next_generation == 0) svc->next_generation++;
    return svc->next_generation;
}

static void sqlite_put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}
static void pack_handle(unsigned char *out, uint32_t value, uint32_t gen) {
    /* A fixed-width lowercase hex token, not a native pointer. */
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i] = (unsigned char)hex[(value >> ((7 - i) * 4)) & 0xf];
        out[8 + i] = (unsigned char)hex[(gen >> ((7 - i) * 4)) & 0xf];
    }
}

static int parse_u32_field(const unsigned char *p, size_t n, unsigned *out) {
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

static int parse_i64_field(const unsigned char *p, size_t n, sqlite3_int64 *out) {
    char buf[32];
    if (n == 0 || n >= sizeof buf) return -1;
    memcpy(buf, p, n);
    buf[n] = 0;
    errno = 0;
    char *end = NULL;
    long long v = strtoll(buf, &end, 10);
    if (errno || !end || *end || end == buf) return -1;
    *out = (sqlite3_int64)v;
    return 0;
}

static int hex_nibble(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int handle_index(const unsigned char *p, size_t n, uint32_t *value,
                        uint32_t *gen) {
    if (n != 16) return -1;
    uint32_t v = 0, g = 0;
    for (int i = 0; i < 8; i++) {
        int a = hex_nibble(p[i]), b = hex_nibble(p[8 + i]);
        if (a < 0 || b < 0) return -1;
        v = (v << 4) | (uint32_t)a;
        g = (g << 4) | (uint32_t)b;
    }
    *value = v;
    *gen = g;
    return *value == 0 ? -1 : 0;
}

static glon_sqlite_db_slot *find_db(glon_sqlite_service *svc,
                                    const unsigned char *p, size_t n,
                                    glon_sqlite_result *r) {
    uint32_t value, gen;
    if (handle_index(p, n, &value, &gen) != 0 || value > svc->cfg.max_dbs) {
        set_error(r, GLON_SQLITE_INVALID_HANDLE, "invalid database handle");
        return NULL;
    }
    glon_sqlite_db_slot *s = &svc->dbs[value - 1];
    if (!s->used) {
        set_error(r, GLON_SQLITE_INVALID_HANDLE, "database is closed");
        return NULL;
    }
    if (s->generation != gen) {
        set_error(r, GLON_SQLITE_STALE_HANDLE, "stale database handle");
        return NULL;
    }
    return s;
}

static glon_sqlite_stmt_slot *find_stmt(glon_sqlite_service *svc,
                                        const unsigned char *p, size_t n,
                                        glon_sqlite_result *r) {
    uint32_t value, gen;
    if (handle_index(p, n, &value, &gen) != 0 || value > svc->cfg.max_stmts) {
        set_error(r, GLON_SQLITE_INVALID_HANDLE, "invalid statement handle");
        return NULL;
    }
    glon_sqlite_stmt_slot *s = &svc->stmts[value - 1];
    if (!s->used) {
        set_error(r, GLON_SQLITE_INVALID_HANDLE, "statement is finalized");
        return NULL;
    }
    if (s->generation != gen) {
        set_error(r, GLON_SQLITE_STALE_HANDLE, "stale statement handle");
        return NULL;
    }
    if (s->db_slot >= svc->cfg.max_dbs || !svc->dbs[s->db_slot].used) {
        set_error(r, GLON_SQLITE_INVALID_HANDLE, "statement database is closed");
        return NULL;
    }
    return s;
}

int glon_sqlite_init(glon_sqlite_service *svc, const glon_sqlite_config *cfg,
                     char *err, size_t errcap) {
    memset(svc, 0, sizeof *svc);
    svc->cfg = *cfg;
    if (!svc->cfg.root || !*svc->cfg.root) svc->cfg.root = ".";
    if (svc->cfg.max_dbs == 0 || svc->cfg.max_dbs > GLON_SQLITE_MAX_DBS)
        svc->cfg.max_dbs = GLON_SQLITE_MAX_DBS;
    if (svc->cfg.max_stmts == 0 || svc->cfg.max_stmts > GLON_SQLITE_MAX_STMTS)
        svc->cfg.max_stmts = GLON_SQLITE_MAX_STMTS;
    if (svc->cfg.max_sql == 0 || svc->cfg.max_sql > GLON_SQLITE_MAX_SQL)
        svc->cfg.max_sql = GLON_SQLITE_MAX_SQL;
    if (svc->cfg.max_value == 0 || svc->cfg.max_value > GLON_SQLITE_MAX_VALUE)
        svc->cfg.max_value = GLON_SQLITE_MAX_VALUE;
    if (svc->cfg.max_row == 0 || svc->cfg.max_row > GLON_SQLITE_MAX_ROW)
        svc->cfg.max_row = GLON_SQLITE_MAX_ROW;
    int st = glon_io_backend_root(svc->cfg.root, svc->root_real,
                                  sizeof svc->root_real, err, errcap);
    if (st != GLON_IO_OK) return -1;
    svc->next_generation = 1;
    return 0;
}

static void finalize_stmt(glon_sqlite_stmt_slot *s) {
    if (s->stmt) sqlite3_finalize((sqlite3_stmt *)s->stmt);
    s->stmt = NULL;
    s->used = 0;
    s->db_slot = 0;
}

void glon_sqlite_destroy(glon_sqlite_service *svc, unsigned *closed_dbs,
                         unsigned *finalized_stmts) {
    unsigned nd = 0, ns = 0;
    for (unsigned i = 0; i < GLON_SQLITE_MAX_STMTS; i++) {
        if (svc->stmts[i].used) { finalize_stmt(&svc->stmts[i]); ns++; }
    }
    for (unsigned i = 0; i < GLON_SQLITE_MAX_DBS; i++) {
        if (svc->dbs[i].used) {
            sqlite3 *db = (sqlite3 *)svc->dbs[i].db;
            if (db) {
                if (sqlite3_close(db) != SQLITE_OK) sqlite3_close_v2(db);
            }
            svc->dbs[i].db = NULL;
            svc->dbs[i].used = 0;
            nd++;
        }
    }
    if (closed_dbs) *closed_dbs = nd;
    if (finalized_stmts) *finalized_stmts = ns;
}

static void close_db_statements(glon_sqlite_service *svc, unsigned slot) {
    for (unsigned i = 0; i < svc->cfg.max_stmts; i++) {
        if (svc->stmts[i].used && svc->stmts[i].db_slot == slot)
            finalize_stmt(&svc->stmts[i]);
    }
}

static int encode_row(sqlite3_stmt *stmt, const glon_sqlite_config *cfg,
                      glon_sqlite_result *r) {
    int n = sqlite3_column_count(stmt);
    if (n < 0 || (unsigned)n > GLON_SQLITE_MAX_COLUMNS ||
        (size_t)n + 1 > cfg->max_row) {
        set_error(r, GLON_SQLITE_TOO_LARGE, "row has too many columns");
        return -1;
    }
    unsigned char *out = r->data;
    size_t off = 0;
    out[off++] = (unsigned char)n;
    for (int i = 0; i < n; i++) {
        int type = sqlite3_column_type(stmt, i);
        if (off + 1 > cfg->max_row) goto too_large;
        switch (type) {
        case SQLITE_NULL:
            out[off++] = 0;
            break;
        case SQLITE_INTEGER: {
            if (off + 9 > cfg->max_row) goto too_large;
            uint64_t v = (uint64_t)sqlite3_column_int64(stmt, i);
            out[off++] = 1;
            for (int k = 7; k >= 0; k--) out[off++] = (unsigned char)(v >> (k * 8));
            break;
        }
        case SQLITE_TEXT:
        case SQLITE_BLOB: {
            const void *p = type == SQLITE_TEXT
                ? (const void *)sqlite3_column_text(stmt, i)
                : sqlite3_column_blob(stmt, i);
            int len = sqlite3_column_bytes(stmt, i);
            if (len < 0 || (size_t)len > cfg->max_value ||
                off + 5 + (size_t)len > cfg->max_row) goto too_large;
            out[off++] = (unsigned char)(type == SQLITE_TEXT ? 2 : 3);
            sqlite_put_u32(out + off, (uint32_t)len); off += 4;
            if (len > 0) memcpy(out + off, p, (size_t)len);
            off += (size_t)len;
            break;
        }
        case SQLITE_FLOAT: {
            char text[64];
            int len = snprintf(text, sizeof text, "%.17g",
                               sqlite3_column_double(stmt, i));
            if (len < 0 || (size_t)len > cfg->max_value ||
                off + 5 + (size_t)len > cfg->max_row) goto too_large;
            out[off++] = 4;
            sqlite_put_u32(out + off, (uint32_t)len); off += 4;
            memcpy(out + off, text, (size_t)len); off += (size_t)len;
            break;
        }
        default:
            set_error(r, GLON_SQLITE_UNSUPPORTED, "unsupported SQLite column type");
            return -1;
        }
    }
    r->len = off;
    return 0;
too_large:
    set_error(r, GLON_SQLITE_TOO_LARGE, "row exceeds configured size limit");
    return -1;
}

static int do_open(glon_sqlite_service *svc, const glon_sqlite_request *req,
                   glon_sqlite_result *r) {
    if (!svc->cfg.allow_read || !svc->cfg.allow_write)
        return (set_error(r, GLON_SQLITE_PERMISSION,
                          "sqlite/open requires file read and write permission"), -1);
    if (req->field_len[0] == 0 || req->field_len[0] >= GLON_SQLITE_PATH_CAP)
        return (set_error(r, GLON_SQLITE_MALFORMED, "invalid database path"), -1);
    char path[GLON_SQLITE_PATH_CAP];
    char err[256];
    int st = glon_io_backend_resolve_path(svc->root_real,
                                          (const char *)req->fields[0],
                                          req->field_len[0], 1,
                                          path, sizeof path, err, sizeof err);
    if (st != GLON_IO_OK) {
        int status = st == GLON_IO_NOT_FOUND ? GLON_SQLITE_NOT_FOUND
                   : st == GLON_IO_PERMISSION ? GLON_SQLITE_PERMISSION
                   : st == GLON_IO_TOO_LARGE ? GLON_SQLITE_TOO_LARGE
                   : GLON_SQLITE_ERROR;
        set_error(r, status, err);
        return -1;
    }
#ifndef _WIN32
    {
        struct stat lst;
        if (lstat(path, &lst) == 0 && S_ISLNK(lst.st_mode)) {
            set_error(r, GLON_SQLITE_PERMISSION, "symlink database path rejected");
            return -1;
        }
    }
#endif
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        set_sqlite_error(r, db, rc, "cannot open database");
        if (db) sqlite3_close(db);
        return -1;
    }
    (void)sqlite3_busy_timeout(db, 0);
    unsigned slot = svc->cfg.max_dbs;
    for (unsigned i = 0; i < svc->cfg.max_dbs; i++) {
        if (!svc->dbs[i].used) { slot = i; break; }
    }
    if (slot == svc->cfg.max_dbs) {
        sqlite3_close(db);
        set_error(r, GLON_SQLITE_TOO_LARGE, "too many open databases");
        return -1;
    }
    uint32_t gen = next_generation(svc);
    svc->dbs[slot].used = 1;
    svc->dbs[slot].db = db;
    svc->dbs[slot].generation = gen;
    pack_handle(r->data, slot + 1, gen);
    r->len = 16;
    r->status = GLON_SQLITE_OK;
    return 0;
}

static int do_prepare(glon_sqlite_service *svc, const glon_sqlite_request *req,
                      glon_sqlite_result *r) {
    glon_sqlite_db_slot *db = find_db(svc, req->fields[0], req->field_len[0], r);
    if (!db) return -1;
    if (req->field_len[1] == 0 || req->field_len[1] > svc->cfg.max_sql)
        return (set_error(r, GLON_SQLITE_TOO_LARGE, "SQL text exceeds configured limit"), -1);
    char sql[GLON_SQLITE_MAX_SQL + 1];
    memcpy(sql, req->fields[1], req->field_len[1]);
    sql[req->field_len[1]] = 0;
    sqlite3_stmt *stmt = NULL;
    const char *tail = NULL;
    int rc = sqlite3_prepare_v2((sqlite3 *)db->db, sql, (int)req->field_len[1],
                                &stmt, &tail);
    if (rc != SQLITE_OK) {
        set_sqlite_error(r, (sqlite3 *)db->db, rc, "cannot prepare SQL");
        if (stmt) sqlite3_finalize(stmt);
        return -1;
    }
    if (!stmt) {
        set_error(r, GLON_SQLITE_MALFORMED, "SQL is empty");
        return -1;
    }
    while (tail && (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n'))
        tail++;
    if (tail && *tail) {
        sqlite3_finalize(stmt);
        set_error(r, GLON_SQLITE_MALFORMED, "only one SQL statement may be prepared");
        return -1;
    }
    unsigned slot = svc->cfg.max_stmts;
    for (unsigned i = 0; i < svc->cfg.max_stmts; i++) {
        if (!svc->stmts[i].used) { slot = i; break; }
    }
    if (slot == svc->cfg.max_stmts) {
        sqlite3_finalize(stmt);
        set_error(r, GLON_SQLITE_TOO_LARGE, "too many prepared statements");
        return -1;
    }
    uint32_t gen = next_generation(svc);
    svc->stmts[slot].used = 1;
    svc->stmts[slot].stmt = stmt;
    svc->stmts[slot].db_slot = (unsigned)(db - svc->dbs);
    svc->stmts[slot].generation = gen;
    pack_handle(r->data, slot + 1, gen);
    r->len = 16;
    r->status = GLON_SQLITE_OK;
    return 0;
}

static int bind_index(glon_sqlite_stmt_slot *s, const unsigned char *p, size_t n,
                      int *index, glon_sqlite_result *r) {
    unsigned idx;
    if (parse_u32_field(p, n, &idx) != 0 || idx == 0 ||
        idx > (unsigned)sqlite3_bind_parameter_count((sqlite3_stmt *)s->stmt)) {
        set_error(r, GLON_SQLITE_MALFORMED, "invalid binding index");
        return -1;
    }
    *index = (int)idx;
    return 0;
}

static int do_bind(glon_sqlite_service *svc, const glon_sqlite_request *req,
                   glon_sqlite_result *r) {
    glon_sqlite_stmt_slot *s = find_stmt(svc, req->fields[0], req->field_len[0], r);
    if (!s) return -1;
    if (req->field_len[1] == 0)
        return (set_error(r, GLON_SQLITE_MALFORMED, "missing binding index"), -1);
    int index = 0;
    if (bind_index(s, req->fields[1], req->field_len[1], &index, r) != 0) return -1;
    int rc;
    if (req->op == GLON_SQLITE_OP_BIND_INT) {
        sqlite3_int64 v;
        if (req->field_len[2] == 0 ||
            parse_i64_field(req->fields[2], req->field_len[2], &v) != 0)
            return (set_error(r, GLON_SQLITE_MALFORMED, "invalid integer binding"), -1);
        rc = sqlite3_bind_int64((sqlite3_stmt *)s->stmt, index, v);
    } else if (req->op == GLON_SQLITE_OP_BIND_TEXT ||
               req->op == GLON_SQLITE_OP_BIND_BLOB) {
        if (req->field_len[2] > svc->cfg.max_value)
            return (set_error(r, GLON_SQLITE_TOO_LARGE, "binding value exceeds configured limit"), -1);
        int is_text = req->op == GLON_SQLITE_OP_BIND_TEXT;
        rc = is_text
            ? sqlite3_bind_text((sqlite3_stmt *)s->stmt, index,
                                (const char *)req->fields[2], (int)req->field_len[2],
                                SQLITE_TRANSIENT)
            : sqlite3_bind_blob((sqlite3_stmt *)s->stmt, index,
                                req->fields[2], (int)req->field_len[2],
                                SQLITE_TRANSIENT);
    } else {
        rc = sqlite3_bind_null((sqlite3_stmt *)s->stmt, index);
    }
    if (rc != SQLITE_OK) {
        set_sqlite_error(r, NULL, rc, "binding failed");
        return -1;
    }
    r->status = GLON_SQLITE_OK;
    r->len = 0;
    return 0;
}

static int do_step(glon_sqlite_service *svc, const glon_sqlite_request *req,
                   glon_sqlite_result *r) {
    glon_sqlite_stmt_slot *s = find_stmt(svc, req->fields[0], req->field_len[0], r);
    if (!s) return -1;
    sqlite3_stmt *stmt = (sqlite3_stmt *)s->stmt;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (encode_row(stmt, &svc->cfg, r) != 0) return -1;
        r->status = GLON_SQLITE_ROW;
        return 0;
    }
    if (rc == SQLITE_DONE) {
        r->status = GLON_SQLITE_DONE;
        r->len = 0;
        return 0;
    }
    set_sqlite_error(r, NULL, rc, "step failed");
    return -1;
}

static int do_exec(glon_sqlite_service *svc, const glon_sqlite_request *req,
                   glon_sqlite_result *r) {
    glon_sqlite_db_slot *db = find_db(svc, req->fields[0], req->field_len[0], r);
    if (!db) return -1;
    if (req->field_len[1] == 0 || req->field_len[1] > svc->cfg.max_sql)
        return (set_error(r, GLON_SQLITE_TOO_LARGE, "SQL text exceeds configured limit"), -1);
    char sql[GLON_SQLITE_MAX_SQL + 1];
    memcpy(sql, req->fields[1], req->field_len[1]);
    sql[req->field_len[1]] = 0;
    char *msg = NULL;
    int rc = sqlite3_exec((sqlite3 *)db->db, sql, NULL, NULL, &msg);
    if (rc != SQLITE_OK) {
        set_error(r, sqlite_status(rc), msg ? msg : "SQL execution failed");
        sqlite3_free(msg);
        return -1;
    }
    sqlite3_free(msg);
    uint64_t changes = (uint64_t)sqlite3_changes((sqlite3 *)db->db);
    for (int i = 7; i >= 0; i--) {
        r->data[i] = (unsigned char)(changes >> ((7 - i) * 8));
    }
    r->len = 8;
    r->status = GLON_SQLITE_OK;
    return 0;
}

int glon_sqlite_execute(glon_sqlite_service *svc,
                        const glon_sqlite_request *req,
                        glon_sqlite_result *r,
                        char *err, size_t errcap) {
    memset(r, 0, sizeof *r);
    r->status = GLON_SQLITE_ERROR;
    switch (req->op) {
    case GLON_SQLITE_OP_OPEN:
        if (req->nfields != 1)
            return (set_error(r, GLON_SQLITE_MALFORMED, "open needs one path"), -1);
        return do_open(svc, req, r);
    case GLON_SQLITE_OP_PREPARE:
        if (req->nfields != 2)
            return (set_error(r, GLON_SQLITE_MALFORMED, "prepare needs db and SQL"), -1);
        return do_prepare(svc, req, r);
    case GLON_SQLITE_OP_BIND_INT:
    case GLON_SQLITE_OP_BIND_TEXT:
    case GLON_SQLITE_OP_BIND_NULL:
    case GLON_SQLITE_OP_BIND_BLOB:
        if (req->nfields < 2 || req->nfields > 3)
            return (set_error(r, GLON_SQLITE_MALFORMED, "bind field count"), -1);
        return do_bind(svc, req, r);
    case GLON_SQLITE_OP_STEP:
        if (req->nfields != 1)
            return (set_error(r, GLON_SQLITE_MALFORMED, "step needs one statement"), -1);
        return do_step(svc, req, r);
    case GLON_SQLITE_OP_RESET:
    case GLON_SQLITE_OP_CLEAR:
    case GLON_SQLITE_OP_FINALIZE: {
        if (req->nfields != 1)
            return (set_error(r, GLON_SQLITE_MALFORMED, "statement operation needs one handle"), -1);
        glon_sqlite_stmt_slot *s = find_stmt(svc, req->fields[0], req->field_len[0], r);
        if (!s) return -1;
        int rc = SQLITE_OK;
        if (req->op == GLON_SQLITE_OP_RESET)
            rc = sqlite3_reset((sqlite3_stmt *)s->stmt);
        else if (req->op == GLON_SQLITE_OP_CLEAR)
            rc = sqlite3_clear_bindings((sqlite3_stmt *)s->stmt);
        else {
            rc = sqlite3_finalize((sqlite3_stmt *)s->stmt);
            s->stmt = NULL;
            s->used = 0;
            s->db_slot = 0;
        }
        if (rc != SQLITE_OK) {
            set_sqlite_error(r, NULL, rc, "statement operation failed");
            return -1;
        }
        r->status = GLON_SQLITE_OK;
        r->len = 0;
        return 0;
    }
    case GLON_SQLITE_OP_EXEC:
        if (req->nfields != 2)
            return (set_error(r, GLON_SQLITE_MALFORMED, "exec needs db and SQL"), -1);
        return do_exec(svc, req, r);
    case GLON_SQLITE_OP_CLOSE: {
        if (req->nfields != 1)
            return (set_error(r, GLON_SQLITE_MALFORMED, "close needs one handle"), -1);
        glon_sqlite_db_slot *db = find_db(svc, req->fields[0], req->field_len[0], r);
        if (!db) return -1;
        unsigned slot = (unsigned)(db - svc->dbs);
        close_db_statements(svc, slot);
        sqlite3 *native = (sqlite3 *)db->db;
        int rc = sqlite3_close(native);
        if (rc != SQLITE_OK) sqlite3_close_v2(native);
        db->db = NULL;
        db->used = 0;
        db->generation = 0;
        r->status = GLON_SQLITE_OK;
        r->len = 0;
        return 0;
    }
    default:
        set_error(r, GLON_SQLITE_MALFORMED, "unknown SQLite operation");
        if (errcap) snprintf(err, errcap, "%s", r->error);
        return -1;
    }
}
