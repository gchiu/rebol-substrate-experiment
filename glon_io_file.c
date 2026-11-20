/* glon_io_file.c - bounded framed protocol and host file-handle service.
 *
 * The host service owns file descriptors and generations.  It accepts only
 * decoded open/read/write/close requests and returns data/status fields; it
 * never sees Glon source, replacement text or application policy.
 */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "glon_io_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

void glon_io_policy_default(glon_io_policy *p) {
    memset(p, 0, sizeof *p);
    p->root = ".";
    p->allow_read = 0;
    p->allow_write = 0;
    p->max_handles = 8;
    p->max_chunk = GLON_IO_MAX_CHUNK;
    p->max_file = GLON_IO_DEFAULT_MAX_FILE;
    p->delay_ms = 0;
    p->short_io = 0;
    p->fail_close = 0;
    p->fail_rename = 0;
}

uint32_t glon_io_get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void glon_io_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

const char *glon_io_status_word(unsigned status) {
    switch (status) {
    case GLON_IO_OK: return "ok";
    case GLON_IO_EOF: return "eof";
    case GLON_IO_NOT_FOUND: return "not-found";
    case GLON_IO_PERMISSION: return "permission";
    case GLON_IO_INVALID_HANDLE: return "invalid-handle";
    case GLON_IO_TOO_LARGE: return "too-large";
    case GLON_IO_IO_ERROR: return "io-error";
    case GLON_IO_UNSUPPORTED: return "unsupported";
    case GLON_IO_BUSY: return "busy";
    case GLON_IO_STALE: return "stale";
    default: return "io-error";
    }
}

int glon_io_service_init(glon_io_service *svc, const glon_io_policy *policy,
                         char *err, size_t errcap) {
    memset(svc, 0, sizeof *svc);
    svc->policy = *policy;
    if (!svc->policy.root || !*svc->policy.root) svc->policy.root = ".";
    if (svc->policy.max_handles == 0) svc->policy.max_handles = 8;
    if (svc->policy.max_handles > GLON_IO_HARD_MAX_HANDLES)
        svc->policy.max_handles = GLON_IO_HARD_MAX_HANDLES;
    if (svc->policy.max_chunk == 0) svc->policy.max_chunk = GLON_IO_MAX_CHUNK;
    if (svc->policy.max_chunk > GLON_IO_MAX_CHUNK)
        svc->policy.max_chunk = GLON_IO_MAX_CHUNK;
    if (svc->policy.max_file == 0) svc->policy.max_file = GLON_IO_DEFAULT_MAX_FILE;
    if (svc->policy.max_file > GLON_IO_HARD_MAX_FILE) {
        if (errcap) snprintf(err, errcap,
                             "io max file %u exceeds hard limit %u",
                             (unsigned)svc->policy.max_file,
                             (unsigned)GLON_IO_HARD_MAX_FILE);
        return -1;
    }
    svc->next_generation = 1;

    if (!realpath(svc->policy.root, svc->root_real)) {
        if (errcap) snprintf(err, errcap, "io root: %s: %s",
                             svc->policy.root, strerror(errno));
        return -1;
    }
    return 0;
}

void glon_io_service_destroy(glon_io_service *svc) {
    unsigned failed = 0;
    (void)glon_io_service_cleanup(svc, &failed);
}

static glon_io_slot *find_slot(glon_io_service *svc, uint32_t value,
                               uint32_t generation, int *index) {
    for (unsigned i = 0; i < GLON_IO_HARD_MAX_HANDLES; i++) {
        glon_io_slot *s = &svc->slots[i];
        if (s->open && s->value == value && s->generation == generation) {
            if (index) *index = (int)i;
            return s;
        }
    }
    return NULL;
}

static unsigned open_count(const glon_io_service *svc) {
    unsigned n = 0;
    for (unsigned i = 0; i < GLON_IO_HARD_MAX_HANDLES; i++)
        if (svc->slots[i].open) n++;
    return n;
}

unsigned glon_io_service_open_handles(const glon_io_service *svc) {
    return open_count(svc);
}

unsigned glon_io_service_cleanup(glon_io_service *svc, unsigned *failed_close) {
    unsigned closed = 0, failed = 0;
    for (unsigned i = 0; i < GLON_IO_HARD_MAX_HANDLES; i++) {
        glon_io_slot *s = &svc->slots[i];
        if (!s->open) continue;
        if (s->fd >= 0) {
            if (glon_io_posix_close(s->fd, NULL, 0) == GLON_IO_OK) closed++;
            else failed++;
        } else closed++;
        if (s->is_temp && s->temp_path[0])
            (void)glon_io_posix_unlink(s->temp_path, NULL, 0);
        s->open = 0;
        s->fd = -1;
        s->is_temp = 0;
        s->temp_path[0] = 0;
    }
    if (failed_close) *failed_close = failed;
    return closed;
}

static int field_nonzero(const glon_io_field *f) { return f && f->len > 0; }

static int decode_handle(const glon_io_field *f, glon_io_handle *h) {
    if (!f || f->len != 8) return -1;
    h->value = glon_io_get_u32(f->bytes);
    h->generation = glon_io_get_u32(f->bytes + 4);
    return 0;
}

static void reply_clear(glon_io_reply *r, uint32_t id, int status) {
    memset(r, 0, sizeof *r);
    r->request_id = id;
    r->status = (uint8_t)status;
}

static int do_open(glon_io_service *svc, const glon_io_request *q,
                   glon_io_reply *r, char *err, size_t errcap) {
    if (q->field_count != 2 || !field_nonzero(&q->fields[0]) ||
        q->fields[1].len != 1)
        return GLON_IO_IO_ERROR;
    int mode = q->fields[1].bytes[0];
    if (mode < GLON_IO_MODE_READ || mode > GLON_IO_MODE_APPEND)
        return GLON_IO_UNSUPPORTED;
    int need_read = (mode == GLON_IO_MODE_READ ||
                     mode == GLON_IO_MODE_READ_WRITE);
    int need_write = (mode == GLON_IO_MODE_WRITE ||
                      mode == GLON_IO_MODE_READ_WRITE ||
                      mode == GLON_IO_MODE_APPEND);
    if ((need_read && !svc->policy.allow_read) ||
        (need_write && !svc->policy.allow_write)) {
        if (errcap) snprintf(err, errcap, "file capability denied");
        return GLON_IO_PERMISSION;
    }
    if (open_count(svc) >= svc->policy.max_handles) {
        if (errcap) snprintf(err, errcap, "maximum open handles reached");
        return GLON_IO_TOO_LARGE;
    }

    int fd = -1;
    int st = glon_io_posix_open(svc->root_real,
                                (const char *)q->fields[0].bytes,
                                q->fields[0].len, mode, &fd, err, errcap);
    if (st != GLON_IO_OK) return st;

    for (unsigned i = 0; i < GLON_IO_HARD_MAX_HANDLES; i++) {
        glon_io_slot *s = &svc->slots[i];
        if (s->open) continue;
        s->open = 1;
        s->fd = fd;
        s->mode = mode;
        s->value = i + 1;
        s->generation = svc->next_generation++;
        if (svc->next_generation == 0) svc->next_generation = 1;
        s->read_total = 0;
        s->write_total = 0;
        glon_io_put_u32(svc->reply_meta, s->value);
        glon_io_put_u32(svc->reply_meta + 4, s->generation);
        r->field_count = 2;
        r->fields[0].bytes = svc->reply_meta;
        r->fields[0].len = 4;
        r->fields[1].bytes = svc->reply_meta + 4;
        r->fields[1].len = 4;
        return GLON_IO_OK;
    }
    (void)glon_io_posix_close(fd, NULL, 0);
    return GLON_IO_TOO_LARGE;
}

static int do_read(glon_io_service *svc, const glon_io_request *q,
                   glon_io_reply *r, char *err, size_t errcap) {
    glon_io_handle h;
    if (q->field_count != 2 || decode_handle(&q->fields[0], &h) != 0 ||
        q->fields[1].len != 4)
        return GLON_IO_IO_ERROR;
    uint32_t requested = glon_io_get_u32(q->fields[1].bytes);
    if (requested == 0) return GLON_IO_IO_ERROR;
    glon_io_slot *s = find_slot(svc, h.value, h.generation, NULL);
    if (!s) return GLON_IO_INVALID_HANDLE;
    if (s->mode != GLON_IO_MODE_READ && s->mode != GLON_IO_MODE_READ_WRITE)
        return GLON_IO_PERMISSION;
    if (s->read_total > svc->policy.max_file) return GLON_IO_TOO_LARGE;
    if (s->read_total == svc->policy.max_file) {
        /* Distinguish an exact-size file from a file that exceeds the cap. */
        uint8_t probe;
        size_t got = 0;
        int st = glon_io_posix_read(s->fd, &probe, 1, &got, err, errcap);
        if (st != GLON_IO_OK) return st;
        return got == 0 ? GLON_IO_EOF : GLON_IO_TOO_LARGE;
    }

    size_t want = requested;
    if (want > svc->policy.max_chunk) want = svc->policy.max_chunk;
    size_t remaining = (size_t)(svc->policy.max_file - s->read_total);
    if (want > remaining) want = remaining;
    if (svc->policy.short_io && want > svc->policy.short_io)
        want = svc->policy.short_io;
    if (want == 0) return GLON_IO_TOO_LARGE;

    size_t got = 0;
    int st = glon_io_posix_read(s->fd, svc->reply_data, want, &got, err, errcap);
    if (st != GLON_IO_OK) return st;
    s->read_total += got;
    if (got == 0) return GLON_IO_EOF;
    r->field_count = 1;
    r->fields[0].bytes = svc->reply_data;
    r->fields[0].len = (uint32_t)got;
    return GLON_IO_OK;
}

static int do_write(glon_io_service *svc, const glon_io_request *q,
                    glon_io_reply *r, char *err, size_t errcap) {
    glon_io_handle h;
    if (q->field_count != 2 || decode_handle(&q->fields[0], &h) != 0)
        return GLON_IO_IO_ERROR;
    glon_io_slot *s = find_slot(svc, h.value, h.generation, NULL);
    if (!s) return GLON_IO_INVALID_HANDLE;
    if (s->mode != GLON_IO_MODE_WRITE && s->mode != GLON_IO_MODE_READ_WRITE &&
        s->mode != GLON_IO_MODE_APPEND)
        return GLON_IO_PERMISSION;
    if (q->fields[1].len == 0) return GLON_IO_IO_ERROR;
    if (q->fields[1].len > svc->policy.max_chunk) return GLON_IO_TOO_LARGE;
    if (s->write_total > svc->policy.max_file) return GLON_IO_TOO_LARGE;
    if ((uint64_t)q->fields[1].len >
        (uint64_t)svc->policy.max_file - s->write_total)
        return GLON_IO_TOO_LARGE;

    size_t want = q->fields[1].len;
    if (svc->policy.short_io && want > svc->policy.short_io)
        want = svc->policy.short_io;
    size_t put = 0;
    int st = glon_io_posix_write(s->fd, q->fields[1].bytes, want, &put,
                                 err, errcap);
    if (st != GLON_IO_OK) return st;
    if (put == 0) return GLON_IO_IO_ERROR;
    s->write_total += put;
    glon_io_put_u32(svc->reply_meta, (uint32_t)put);
    r->field_count = 1;
    r->fields[0].bytes = svc->reply_meta;
    r->fields[0].len = 4;
    return GLON_IO_OK;
}

static void clear_slot(glon_io_slot *s) {
    s->open = 0;
    s->fd = -1;
    s->is_temp = 0;
    s->temp_path[0] = 0;
    s->source_real[0] = 0;
    s->parent_real[0] = 0;
    s->have_source_stat = 0;
}

static int do_temp_open(glon_io_service *svc, const glon_io_request *q,
                        glon_io_reply *r, char *err, size_t errcap) {
    if (q->field_count != 1 || !field_nonzero(&q->fields[0]))
        return GLON_IO_IO_ERROR;
    if (!svc->policy.allow_write) {
        if (errcap) snprintf(err, errcap, "file write capability denied");
        return GLON_IO_PERMISSION;
    }
    if (open_count(svc) >= svc->policy.max_handles) {
        if (errcap) snprintf(err, errcap, "maximum open handles reached");
        return GLON_IO_TOO_LARGE;
    }
    int fd = -1;
    char temp_path[GLON_IO_PATH_CAP], source_real[GLON_IO_PATH_CAP];
    char parent_real[GLON_IO_PATH_CAP];
    unsigned long long dev = 0, ino = 0, size = 0;
    long mtime_sec = 0, mtime_nsec = 0;
    unsigned mode = 0;
    int st = glon_io_posix_make_temp(svc->root_real,
                                     (const char *)q->fields[0].bytes,
                                     q->fields[0].len, &fd,
                                     temp_path, sizeof temp_path,
                                     source_real, sizeof source_real,
                                     parent_real, sizeof parent_real,
                                     &dev, &ino, &size, &mtime_sec, &mtime_nsec,
                                     &mode, err, errcap);
    if (st != GLON_IO_OK) return st;
    for (unsigned i = 0; i < GLON_IO_HARD_MAX_HANDLES; i++) {
        glon_io_slot *s = &svc->slots[i];
        if (s->open) continue;
        memset(s, 0, sizeof *s);
        s->open = 1;
        s->fd = fd;
        s->mode = GLON_IO_MODE_WRITE;
        s->value = i + 1;
        s->generation = svc->next_generation++;
        if (svc->next_generation == 0) svc->next_generation = 1;
        s->is_temp = 1;
        s->have_source_stat = 1;
        s->source_dev = dev;
        s->source_ino = ino;
        s->source_size = size;
        s->source_mtime_sec = mtime_sec;
        s->source_mtime_nsec = mtime_nsec;
        s->source_mode = mode;
        snprintf(s->temp_path, sizeof s->temp_path, "%s", temp_path);
        snprintf(s->source_real, sizeof s->source_real, "%s", source_real);
        snprintf(s->parent_real, sizeof s->parent_real, "%s", parent_real);
        glon_io_put_u32(svc->reply_meta, s->value);
        glon_io_put_u32(svc->reply_meta + 4, s->generation);
        r->field_count = 2;
        r->fields[0].bytes = svc->reply_meta;
        r->fields[0].len = 4;
        r->fields[1].bytes = svc->reply_meta + 4;
        r->fields[1].len = 4;
        return GLON_IO_OK;
    }
    (void)glon_io_posix_close(fd, NULL, 0);
    (void)glon_io_posix_unlink(temp_path, NULL, 0);
    return GLON_IO_TOO_LARGE;
}

static int do_commit(glon_io_service *svc, const glon_io_request *q,
                     glon_io_reply *r, char *err, size_t errcap) {
    (void)r;
    glon_io_handle h;
    if (q->field_count != 2 || decode_handle(&q->fields[0], &h) != 0 ||
        !field_nonzero(&q->fields[1]))
        return GLON_IO_IO_ERROR;
    if (!svc->policy.allow_write) {
        if (errcap) snprintf(err, errcap, "file write capability denied");
        return GLON_IO_PERMISSION;
    }
    glon_io_slot *s = find_slot(svc, h.value, h.generation, NULL);
    if (!s || !s->is_temp) return GLON_IO_INVALID_HANDLE;
    int fd = s->fd;
    s->fd = -1;
    int close_status = GLON_IO_OK;
    if (fd >= 0) {
        if (fchmod(fd, (mode_t)(s->source_mode & 07777)) != 0) {
            if (errcap) snprintf(err, errcap, "temp chmod: %s", strerror(errno));
            close_status = GLON_IO_IO_ERROR;
            (void)glon_io_posix_close(fd, NULL, 0);
            fd = -1;
        } else {
            close_status = glon_io_posix_close(fd, err, errcap);
        }
    }
    int st = close_status;
    if (st == GLON_IO_OK && svc->policy.fail_close) {
        if (errcap) snprintf(err, errcap, "injected close failure");
        st = GLON_IO_IO_ERROR;
    }
    if (st == GLON_IO_OK && svc->policy.fail_rename) {
        if (errcap) snprintf(err, errcap, "injected rename failure");
        st = GLON_IO_IO_ERROR;
    }
    if (st == GLON_IO_OK) {
        st = glon_io_posix_commit(svc->root_real,
                                  (const char *)q->fields[1].bytes,
                                  q->fields[1].len, s->temp_path,
                                  s->parent_real, s->source_real,
                                  s->source_dev, s->source_ino, s->source_size,
                                  s->source_mtime_sec, s->source_mtime_nsec,
                                  s->source_mode, err, errcap);
    }
    if (st == GLON_IO_OK) {
        clear_slot(s);
    } else {
        /* All-or-nothing commit: on any failure the temp is removed and the
         * original path is untouched. */
        (void)glon_io_posix_unlink(s->temp_path, NULL, 0);
        clear_slot(s);
    }
    return st;
}

static int do_discard(glon_io_service *svc, const glon_io_request *q,
                      glon_io_reply *r, char *err, size_t errcap) {
    (void)r;
    glon_io_handle h;
    if (q->field_count != 1 || decode_handle(&q->fields[0], &h) != 0)
        return GLON_IO_IO_ERROR;
    glon_io_slot *s = find_slot(svc, h.value, h.generation, NULL);
    if (!s || !s->is_temp) return GLON_IO_INVALID_HANDLE;
    int st = GLON_IO_OK;
    if (s->fd >= 0) {
        int fd = s->fd;
        s->fd = -1;
        st = glon_io_posix_close(fd, err, errcap);
    }
    int ust = glon_io_posix_unlink(s->temp_path, err, errcap);
    if (st == GLON_IO_OK) st = ust;
    clear_slot(s);
    return st;
}

static int do_close(glon_io_service *svc, const glon_io_request *q,
                    glon_io_reply *r, char *err, size_t errcap) {
    (void)r;
    glon_io_handle h;
    if (q->field_count != 1 || decode_handle(&q->fields[0], &h) != 0)
        return GLON_IO_IO_ERROR;
    glon_io_slot *s = find_slot(svc, h.value, h.generation, NULL);
    if (!s) return GLON_IO_INVALID_HANDLE;
    int fd = s->fd;
    s->open = 0;
    s->fd = -1;
    if (svc->policy.fail_close) {
        (void)glon_io_posix_close(fd, NULL, 0);
        if (errcap) snprintf(err, errcap, "injected close failure");
        return GLON_IO_IO_ERROR;
    }
    return glon_io_posix_close(fd, err, errcap);
}

int glon_io_execute(glon_io_service *svc, const glon_io_request *q,
                    glon_io_reply *r, char *err, size_t errcap) {
    reply_clear(r, q->request_id, GLON_IO_OK);
    int st;
    switch (q->op) {
    case GLON_IO_OP_OPEN:  st = do_open(svc, q, r, err, errcap); break;
    case GLON_IO_OP_READ:  st = do_read(svc, q, r, err, errcap); break;
    case GLON_IO_OP_WRITE: st = do_write(svc, q, r, err, errcap); break;
    case GLON_IO_OP_CLOSE: st = do_close(svc, q, r, err, errcap); break;
    case GLON_IO_OP_TEMP_OPEN: st = do_temp_open(svc, q, r, err, errcap); break;
    case GLON_IO_OP_COMMIT: st = do_commit(svc, q, r, err, errcap); break;
    case GLON_IO_OP_DISCARD: st = do_discard(svc, q, r, err, errcap); break;
    default: st = GLON_IO_UNSUPPORTED; break;
    }
    if (st != GLON_IO_OK) {
        r->status = (uint8_t)st;
        r->field_count = 0;
    }
    return st;
}

int glon_io_decode_request(const uint8_t *frame, size_t len,
                           glon_io_request *req, char *err, size_t errcap) {
    memset(req, 0, sizeof *req);
    if (len < 7 || frame[0] != GLON_IO_REQUEST_MAGIC) {
        if (errcap) snprintf(err, errcap, "bad request magic/length");
        return -1;
    }
    req->op = frame[1];
    req->request_id = glon_io_get_u32(frame + 2);
    req->field_count = frame[6];
    if (req->request_id == 0 || req->field_count > GLON_IO_MAX_FIELDS) {
        if (errcap) snprintf(err, errcap, "bad request id/field count");
        return -1;
    }
    size_t off = 7;
    for (unsigned i = 0; i < req->field_count; i++) {
        if (off + 4 > len) {
            if (errcap) snprintf(err, errcap, "truncated field length");
            return -1;
        }
        uint32_t n = glon_io_get_u32(frame + off);
        off += 4;
        if (n > GLON_IO_MAX_FRAME || off + n > len) {
            if (errcap) snprintf(err, errcap, "truncated/oversized field");
            return -1;
        }
        req->fields[i].bytes = frame + off;
        req->fields[i].len = n;
        off += n;
    }
    if (off != len) {
        if (errcap) snprintf(err, errcap, "trailing request bytes");
        return -1;
    }
    return 0;
}

int glon_io_encode_reply(const glon_io_reply *r,
                         uint8_t *out, size_t cap, size_t *out_len,
                         char *err, size_t errcap) {
    *out_len = 0;
    if (r->field_count > 2) {
        if (errcap) snprintf(err, errcap, "too many reply fields");
        return -1;
    }
    size_t need = 7;
    for (unsigned i = 0; i < r->field_count; i++) need += 4 + r->fields[i].len;
    if (need > cap || need > GLON_IO_MAX_FRAME) {
        if (errcap) snprintf(err, errcap, "reply frame exceeds %u bytes",
                             (unsigned)GLON_IO_MAX_FRAME);
        return -1;
    }
    out[0] = GLON_IO_REPLY_MAGIC;
    glon_io_put_u32(out + 1, r->request_id);
    out[5] = r->status;
    out[6] = r->field_count;
    size_t off = 7;
    for (unsigned i = 0; i < r->field_count; i++) {
        glon_io_put_u32(out + off, r->fields[i].len);
        off += 4;
        memcpy(out + off, r->fields[i].bytes, r->fields[i].len);
        off += r->fields[i].len;
    }
    *out_len = off;
    return 0;
}
