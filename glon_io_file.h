/* glon_io_file.h - optional host-mediated asynchronous file I/O for Glon.
 *
 * This module lives outside the frozen S1 substrate and outside the evaluator.
 * It implements the length-framed request/reply contract in
 * docs/design/GLON-OPTIONAL-IO.md, Approach B.  Glon builds request frames and
 * consumes reply fields; the host only moves bytes and owns filesystem
 * handles.  No S1 primitive, HOST id, parser or evaluator code is changed.
 *
 * The native CLI's optional driver is in glon_io_file_cli.c.  The protocol
 * here is also usable by a future host without exposing it as a Glon word.
 */
#ifndef GLON_IO_FILE_H
#define GLON_IO_FILE_H

#include <stddef.h>
#include <stdint.h>

#define GLON_IO_REQUEST_MAGIC  0x01u  /* protocol v1 */
#define GLON_IO_REPLY_MAGIC    0x81u  /* protocol v1 */
#define GLON_IO_MAX_FIELDS     4u
#define GLON_IO_MAX_FRAME      8192u
#define GLON_IO_MAX_CHUNK      400u
/* Whole-file limit measured against the 7,232-cell managed heap.  The
 * recipe needs input + replacement + write slice live together; 1 KiB is the
 * conservative safe bound.  Larger values are rejected, not clamped. */
#define GLON_IO_DEFAULT_MAX_FILE 1024u
#define GLON_IO_SAFE_MAX_FILE    1024u
#define GLON_IO_PROTOCOL_VERSION 1u
#define GLON_IO_HARD_MAX_HANDLES 16u

typedef enum {
    GLON_IO_OK = 0,
    GLON_IO_EOF,
    GLON_IO_NOT_FOUND,
    GLON_IO_PERMISSION,
    GLON_IO_INVALID_HANDLE,
    GLON_IO_TOO_LARGE,
    GLON_IO_IO_ERROR,
    GLON_IO_UNSUPPORTED,
    GLON_IO_BUSY
} glon_io_status;

typedef enum {
    GLON_IO_MODE_READ = 1,
    GLON_IO_MODE_WRITE = 2,
    GLON_IO_MODE_READ_WRITE = 3
} glon_io_mode;

typedef enum {
    GLON_IO_OP_OPEN = 1,
    GLON_IO_OP_READ = 2,
    GLON_IO_OP_WRITE = 3,
    GLON_IO_OP_CLOSE = 4
} glon_io_op;

typedef struct {
    uint32_t value;
    uint32_t generation;
} glon_io_handle;

typedef struct {
    const char *root;          /* logical root; canonicalised by service_init */
    int allow_read;
    int allow_write;
    unsigned max_handles;      /* 1..GLON_IO_HARD_MAX_HANDLES */
    unsigned max_chunk;        /* bounded reply/request data */
    uint32_t max_file;         /* per-open cumulative byte limit */
    unsigned delay_ms;         /* test-only worker delay before the operation */
    unsigned short_io;         /* test-only cap on one read/write result */
} glon_io_policy;

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} glon_io_field;

typedef struct {
    uint8_t op;
    uint32_t request_id;
    uint8_t field_count;
    glon_io_field fields[GLON_IO_MAX_FIELDS];
} glon_io_request;

typedef struct {
    uint32_t request_id;
    uint8_t status;
    uint8_t field_count;
    glon_io_field fields[2];
} glon_io_reply;

typedef struct {
    int open;
    int fd;
    int mode;
    uint32_t value;
    uint32_t generation;
    uint64_t read_total;
    uint64_t write_total;
} glon_io_slot;

typedef struct {
    glon_io_policy policy;
    char root_real[4096];
    glon_io_slot slots[GLON_IO_HARD_MAX_HANDLES];
    uint32_t next_generation;
    unsigned char reply_data[GLON_IO_MAX_CHUNK];
    unsigned char reply_meta[16];
} glon_io_service;

void glon_io_policy_default(glon_io_policy *policy);

int glon_io_service_init(glon_io_service *svc, const glon_io_policy *policy,
                         char *err, size_t errcap);
void glon_io_service_destroy(glon_io_service *svc);

/* Parse and validate a request frame.  Field pointers refer into `frame`. */
int glon_io_decode_request(const uint8_t *frame, size_t len,
                           glon_io_request *req, char *err, size_t errcap);

/* Execute one decoded request.  The reply's fields point into storage owned by
 * svc and remain valid until the next execute/cleanup call. */
int glon_io_execute(glon_io_service *svc, const glon_io_request *req,
                    glon_io_reply *reply, char *err, size_t errcap);

int glon_io_encode_reply(const glon_io_reply *reply,
                         uint8_t *out, size_t cap, size_t *out_len,
                         char *err, size_t errcap);

/* Close every open handle; returns the number closed. */
unsigned glon_io_service_cleanup(glon_io_service *svc, unsigned *failed_close);
unsigned glon_io_service_open_handles(const glon_io_service *svc);

uint32_t glon_io_get_u32(const uint8_t *p);
void glon_io_put_u32(uint8_t *p, uint32_t v);
const char *glon_io_status_word(unsigned status);

/* POSIX backend (glon_io_file_posix.c).  These return a glon_io_status. */
int glon_io_posix_open(const char *root_real, const char *name, size_t name_len,
                       int mode, int *fd, char *err, size_t errcap);
int glon_io_posix_read(int fd, uint8_t *buf, size_t cap, size_t *got,
                       char *err, size_t errcap);
int glon_io_posix_write(int fd, const uint8_t *buf, size_t len, size_t *put,
                        char *err, size_t errcap);
int glon_io_posix_close(int fd, char *err, size_t errcap);

#endif /* GLON_IO_FILE_H */
