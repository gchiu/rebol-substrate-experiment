/* glon_sqlite.h - optional general-purpose SQLite capability for the Glon CLI.
 *
 * The host owns sqlite3 database/statement pointers.  Glon only sees bounded,
 * opaque 8-byte handle strings carrying (slot, generation).  Paths are
 * resolved through the same canonical, logical-name confinement as the file
 * module.  No application, user, session or chat policy lives here.
 */
#ifndef GLON_SQLITE_H
#define GLON_SQLITE_H

#include <stddef.h>
#include <stdint.h>

#define GLON_SQLITE_MAX_DBS 8u
#define GLON_SQLITE_MAX_STMTS 32u
#define GLON_SQLITE_MAX_FIELDS 3u
#define GLON_SQLITE_FIELD_CAP 8192u
#define GLON_SQLITE_MAX_SQL 8192u
#define GLON_SQLITE_MAX_VALUE 4096u
#define GLON_SQLITE_MAX_ROW 8192u
#define GLON_SQLITE_MAX_COLUMNS 32u
#define GLON_SQLITE_PATH_CAP 4096u

enum {
    GLON_SQLITE_OP_OPEN = 1,
    GLON_SQLITE_OP_PREPARE = 2,
    GLON_SQLITE_OP_BIND_INT = 3,
    GLON_SQLITE_OP_BIND_TEXT = 4,
    GLON_SQLITE_OP_BIND_NULL = 5,
    GLON_SQLITE_OP_BIND_BLOB = 6,
    GLON_SQLITE_OP_STEP = 7,
    GLON_SQLITE_OP_RESET = 8,
    GLON_SQLITE_OP_CLEAR = 9,
    GLON_SQLITE_OP_FINALIZE = 10,
    GLON_SQLITE_OP_EXEC = 11,
    GLON_SQLITE_OP_CLOSE = 12
};

enum {
    GLON_SQLITE_OK = 0,
    GLON_SQLITE_ROW,
    GLON_SQLITE_DONE,
    GLON_SQLITE_BUSY,
    GLON_SQLITE_CONSTRAINT,
    GLON_SQLITE_MALFORMED,
    GLON_SQLITE_INVALID_HANDLE,
    GLON_SQLITE_STALE_HANDLE,
    GLON_SQLITE_TOO_LARGE,
    GLON_SQLITE_PERMISSION,
    GLON_SQLITE_NOT_FOUND,
    GLON_SQLITE_OOM,
    GLON_SQLITE_READONLY,
    GLON_SQLITE_ERROR,
    GLON_SQLITE_UNSUPPORTED
};

typedef struct {
    const char *root;          /* --io-root; default "." */
    int allow_read;            /* required for sqlite/open */
    int allow_write;           /* required for sqlite/open */
    unsigned max_dbs;          /* bounded by GLON_SQLITE_MAX_DBS */
    unsigned max_stmts;        /* bounded by GLON_SQLITE_MAX_STMTS */
    unsigned max_sql;          /* bounded by GLON_SQLITE_MAX_SQL */
    unsigned max_value;        /* bounded by GLON_SQLITE_MAX_VALUE */
    unsigned max_row;          /* bounded by GLON_SQLITE_MAX_ROW */
} glon_sqlite_config;

typedef struct {
    unsigned op;
    unsigned nfields;
    unsigned char fields[GLON_SQLITE_MAX_FIELDS][GLON_SQLITE_FIELD_CAP];
    size_t field_len[GLON_SQLITE_MAX_FIELDS];
} glon_sqlite_request;

/* `data` is a handle (8 bytes) for OPEN/PREPARE, changed-row count (8 bytes)
 * for EXEC, or an encoded row for STEP/ROW.  On failure it holds the error
 * text used by the Glon failure closure. */
typedef struct {
    int status;
    char error[256];
    unsigned char data[GLON_SQLITE_MAX_ROW];
    size_t len;
} glon_sqlite_result;

void glon_sqlite_config_default(glon_sqlite_config *cfg);
const char *glon_sqlite_status_word(int status);

typedef struct {
    int used;
    void *db;
    uint32_t generation;
} glon_sqlite_db_slot;

typedef struct {
    int used;
    void *stmt;
    unsigned db_slot;
    uint32_t generation;
} glon_sqlite_stmt_slot;

typedef struct {
    glon_sqlite_config cfg;
    char root_real[GLON_SQLITE_PATH_CAP];
    glon_sqlite_db_slot dbs[GLON_SQLITE_MAX_DBS];
    glon_sqlite_stmt_slot stmts[GLON_SQLITE_MAX_STMTS];
    uint32_t next_generation;
} glon_sqlite_service;

int glon_sqlite_init(glon_sqlite_service *svc, const glon_sqlite_config *cfg,
                     char *err, size_t errcap);
void glon_sqlite_destroy(glon_sqlite_service *svc, unsigned *closed_dbs,
                         unsigned *finalized_stmts);
int glon_sqlite_execute(glon_sqlite_service *svc,
                        const glon_sqlite_request *req,
                        glon_sqlite_result *result,
                        char *err, size_t errcap);

#endif /* GLON_SQLITE_H */
