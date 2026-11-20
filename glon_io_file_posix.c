/* glon_io_file_posix.c - POSIX path confinement and byte backend.
 *
 * Paths are logical names under one host-canonicalised root.  Absolute paths,
 * drive/UNC prefixes, backslashes, empty components and "."/".." components
 * are rejected.  Existing targets and write parents are resolved with
 * realpath() and must remain under the root.  O_NOFOLLOW rejects a final
 * symlink, so a symlink cannot be swapped in after resolution.  This is a
 * proof-of-concept host policy: it does not claim to eliminate every TOCTOU
 * race in a hostile multi-process environment.
 */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "glon_io_file.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define IO_PATH_CAP 4096

static int under_root(const char *root, const char *path) {
    size_t n = strlen(root);
    if (n == 0) return 0;
    if (strcmp(root, "/") == 0) return path[0] == '/';
    if (strncmp(path, root, n) != 0) return 0;
    return path[n] == '/' || path[n] == '\0';
}

static int name_ok(const char *name, size_t len) {
    if (len == 0 || len >= IO_PATH_CAP) return 0;
    if (name[0] == '/' || name[0] == '\\') return 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || name[i] == '/') {
            size_t part = i - start;
            if (part == 0) return 0;
            if (part == 1 && name[start] == '.') return 0;
            if (part == 2 && name[start] == '.' && name[start + 1] == '.') return 0;
            start = i + 1;
        } else if (name[i] == '\\' || name[i] == ':' || name[i] == '\0') {
            return 0;
        }
    }
    return 1;
}

static int status_from_errno(int e) {
    switch (e) {
    case ENOENT:
    case ENOTDIR:
        return GLON_IO_NOT_FOUND;
    case EACCES:
    case EPERM:
    case ELOOP:
        return GLON_IO_PERMISSION;
    case ENAMETOOLONG:
    case EFBIG:
    case EOVERFLOW:
        return GLON_IO_TOO_LARGE;
    default:
        return GLON_IO_IO_ERROR;
    }
}

static int fail(char *err, size_t cap, const char *op, const char *path) {
    if (cap) snprintf(err, cap, "%s: %s: %s", op, path, strerror(errno));
    return status_from_errno(errno);
}

static int resolve_existing(const char *root, const char *joined,
                            char *out, size_t outcap,
                            char *err, size_t errcap) {
    char resolved[IO_PATH_CAP];
    if (!realpath(joined, resolved))
        return fail(err, errcap, "realpath", joined);
    if (!under_root(root, resolved)) {
        if (errcap) snprintf(err, errcap, "path escapes io root: %s", joined);
        return GLON_IO_PERMISSION;
    }
    if (strlen(resolved) + 1 > outcap) {
        if (errcap) snprintf(err, errcap, "path too long: %s", joined);
        return GLON_IO_TOO_LARGE;
    }
    strcpy(out, resolved);
    return GLON_IO_OK;
}

static int resolve_for_write(const char *root, const char *joined,
                             char *out, size_t outcap,
                             char *err, size_t errcap) {
    char parent[IO_PATH_CAP], parent_real[IO_PATH_CAP], base[IO_PATH_CAP];
    size_t n = strlen(joined);
    const char *slash = strrchr(joined, '/');
    if (!slash) {
        if (errcap) snprintf(err, errcap, "invalid path: %s", joined);
        return GLON_IO_IO_ERROR;
    }
    size_t plen = (size_t)(slash - joined);
    if (plen == 0) plen = 1;
    if (plen >= sizeof parent || strlen(slash + 1) >= sizeof base) {
        if (errcap) snprintf(err, errcap, "path too long: %s", joined);
        return GLON_IO_TOO_LARGE;
    }
    memcpy(parent, joined, plen);
    parent[plen] = 0;
    strcpy(base, slash + 1);
    (void)n;

    if (!realpath(parent, parent_real))
        return fail(err, errcap, "realpath", parent);
    if (!under_root(root, parent_real)) {
        if (errcap) snprintf(err, errcap, "path escapes io root: %s", joined);
        return GLON_IO_PERMISSION;
    }
    if (strlen(parent_real) + 1 + strlen(base) + 1 > outcap) {
        if (errcap) snprintf(err, errcap, "path too long: %s", joined);
        return GLON_IO_TOO_LARGE;
    }
    snprintf(out, outcap, "%s/%s", parent_real, base);
    return GLON_IO_OK;
}

int glon_io_posix_open(const char *root_real, const char *name, size_t name_len,
                       int mode, int *fd, char *err, size_t errcap) {
    *fd = -1;
    if (!name_ok(name, name_len)) {
        if (errcap) snprintf(err, errcap, "invalid logical file name");
        return GLON_IO_PERMISSION;
    }

    char joined[IO_PATH_CAP];
    int n = snprintf(joined, sizeof joined, "%s/%.*s", root_real,
                     (int)name_len, name);
    if (n < 0 || (size_t)n >= sizeof joined) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }

    char resolved[IO_PATH_CAP];
    int st;
    int flags;
    if (mode == GLON_IO_MODE_WRITE || mode == GLON_IO_MODE_APPEND) {
        st = resolve_for_write(root_real, joined, resolved, sizeof resolved,
                               err, errcap);
        if (st != GLON_IO_OK) return st;
        flags = (mode == GLON_IO_MODE_APPEND
                 ? O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW
                 : O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW);
    } else {
        st = resolve_existing(root_real, joined, resolved, sizeof resolved,
                              err, errcap);
        if (st != GLON_IO_OK) return st;
        flags = (mode == GLON_IO_MODE_READ_WRITE ? O_RDWR : O_RDONLY) | O_NOFOLLOW;
    }

    int f = open(resolved, flags, 0600);
    if (f < 0) return fail(err, errcap, "open", resolved);
    *fd = f;
    return GLON_IO_OK;
}

int glon_io_posix_read(int fd, uint8_t *buf, size_t cap, size_t *got,
                       char *err, size_t errcap) {
    ssize_t n;
    do { n = read(fd, buf, cap); } while (n < 0 && errno == EINTR);
    if (n < 0) {
        *got = 0;
        return fail(err, errcap, "read", "handle");
    }
    *got = (size_t)n;
    return GLON_IO_OK;
}

int glon_io_posix_write(int fd, const uint8_t *buf, size_t len, size_t *put,
                        char *err, size_t errcap) {
    ssize_t n;
    do { n = write(fd, buf, len); } while (n < 0 && errno == EINTR);
    if (n < 0) {
        *put = 0;
        return fail(err, errcap, "write", "handle");
    }
    *put = (size_t)n;
    return GLON_IO_OK;
}

int glon_io_posix_close(int fd, char *err, size_t errcap) {
    if (close(fd) != 0) return fail(err, errcap, "close", "handle");
    return GLON_IO_OK;
}

/* ---- protocol v2: temp file and atomic replace helpers ------------------ */

static unsigned long long temp_counter;
static unsigned long long random_bits(void) {
    unsigned long long v = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t n = read(fd, &v, sizeof v);
        (void)close(fd);
        if (n == (ssize_t)sizeof v) return v;
    }
    return ((unsigned long long)time(NULL) << 32) ^
           ((unsigned long long)getpid() << 16) ^ (++temp_counter);
}

int glon_io_posix_make_temp(const char *root_real, const char *name,
                            size_t name_len, int *fd,
                            char *temp_path, size_t temp_cap,
                            char *source_real, size_t source_cap,
                            char *parent_real, size_t parent_cap,
                            unsigned long long *dev, unsigned long long *ino,
                            unsigned long long *size, long *mtime_sec,
                            long *mtime_nsec, unsigned *mode,
                            char *err, size_t errcap) {
    *fd = -1;
    if (!name_ok(name, name_len)) {
        if (errcap) snprintf(err, errcap, "invalid logical file name");
        return GLON_IO_PERMISSION;
    }
    char joined[IO_PATH_CAP];
    int n = snprintf(joined, sizeof joined, "%s/%.*s", root_real,
                     (int)name_len, name);
    if (n < 0 || (size_t)n >= sizeof joined) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    struct stat lst;
    if (lstat(joined, &lst) != 0) return fail(err, errcap, "lstat", joined);
    if (S_ISLNK(lst.st_mode)) {
        if (errcap) snprintf(err, errcap, "symlink source rejected: %s", joined);
        return GLON_IO_PERMISSION;
    }
    int st = resolve_existing(root_real, joined, source_real, source_cap,
                              err, errcap);
    if (st != GLON_IO_OK) return st;
    struct stat sb;
    if (stat(source_real, &sb) != 0) return fail(err, errcap, "stat", source_real);
    if (!S_ISREG(sb.st_mode)) {
        if (errcap) snprintf(err, errcap, "not a regular file: %s", source_real);
        return GLON_IO_UNSUPPORTED;
    }
    size_t len = strlen(source_real);
    if (len == 0 || len >= parent_cap) {
        if (errcap) snprintf(err, errcap, "source path too long");
        return GLON_IO_TOO_LARGE;
    }
    memcpy(parent_real, source_real, len + 1);
    char *slash = strrchr(parent_real, '/');
    if (!slash) {
        if (errcap) snprintf(err, errcap, "invalid source path");
        return GLON_IO_IO_ERROR;
    }
    if (slash == parent_real) slash[1] = 0;
    else *slash = 0;

    for (int attempt = 0; attempt < 64; attempt++) {
        unsigned long long r = random_bits();
        int m = snprintf(temp_path, temp_cap, "%s/.glon-tmp-%ld-%llx-%llx",
                         parent_real, (long)getpid(),
                         temp_counter + (unsigned long long)attempt, r);
        if (m < 0 || (size_t)m >= temp_cap) {
            if (errcap) snprintf(err, errcap, "temporary path too long");
            return GLON_IO_TOO_LARGE;
        }
        int f = open(temp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
                     O_CLOEXEC, 0600);
        if (f < 0) {
            if (errno == EEXIST) continue;
            return fail(err, errcap, "temp open", temp_path);
        }
        temp_counter += (unsigned long long)attempt + 1;
        /* Keep the unfinished file private (0600).  Commit applies the
         * source permission bits only after all data is written and the
         * source has been checked. */
        *fd = f;
        *dev = (unsigned long long)sb.st_dev;
        *ino = (unsigned long long)sb.st_ino;
        *size = (unsigned long long)sb.st_size;
        *mtime_sec = (long)sb.st_mtim.tv_sec;
        *mtime_nsec = (long)sb.st_mtim.tv_nsec;
        *mode = (unsigned)sb.st_mode;
        return GLON_IO_OK;
    }
    if (errcap) snprintf(err, errcap, "cannot create a unique temporary file");
    return GLON_IO_IO_ERROR;
}

int glon_io_posix_commit(const char *root_real, const char *target_name,
                         size_t target_len, const char *temp_path,
                         const char *parent_real, const char *source_real,
                         unsigned long long dev, unsigned long long ino,
                         unsigned long long size, long mtime_sec,
                         long mtime_nsec, unsigned mode,
                         char *err, size_t errcap) {
    if (!name_ok(target_name, target_len)) {
        if (errcap) snprintf(err, errcap, "invalid logical file name");
        return GLON_IO_PERMISSION;
    }
    char joined[IO_PATH_CAP];
    int n = snprintf(joined, sizeof joined, "%s/%.*s", root_real,
                     (int)target_len, target_name);
    if (n < 0 || (size_t)n >= sizeof joined) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    char resolved[IO_PATH_CAP];
    int st = resolve_for_write(root_real, joined, resolved, sizeof resolved,
                               err, errcap);
    if (st != GLON_IO_OK) return st;
    char parent_check[IO_PATH_CAP];
    if (strlen(resolved) >= sizeof parent_check) {
        if (errcap) snprintf(err, errcap, "target path too long");
        return GLON_IO_TOO_LARGE;
    }
    strcpy(parent_check, resolved);
    char *slash = strrchr(parent_check, '/');
    if (!slash) {
        if (errcap) snprintf(err, errcap, "invalid target path");
        return GLON_IO_IO_ERROR;
    }
    if (slash == parent_check) slash[1] = 0;
    else *slash = 0;
    if (strcmp(parent_check, parent_real) != 0) {
        if (errcap) snprintf(err, errcap,
                             "temporary file and target must share a directory");
        return GLON_IO_IO_ERROR;
    }
    struct stat tl;
    if (lstat(resolved, &tl) == 0) {
        if (S_ISLNK(tl.st_mode)) {
            if (errcap) snprintf(err, errcap, "symlink target rejected: %s", resolved);
            return GLON_IO_PERMISSION;
        }
        if (!S_ISREG(tl.st_mode)) {
            if (errcap) snprintf(err, errcap, "not a regular target: %s", resolved);
            return GLON_IO_UNSUPPORTED;
        }
    } else if (errno != ENOENT) {
        return fail(err, errcap, "lstat", resolved);
    }
    struct stat sb;
    if (stat(source_real, &sb) != 0) return fail(err, errcap, "stat", source_real);
    if ((unsigned long long)sb.st_dev != dev ||
        (unsigned long long)sb.st_ino != ino ||
        (unsigned long long)sb.st_size != size ||
        (long)sb.st_mtim.tv_sec != mtime_sec ||
        (long)sb.st_mtim.tv_nsec != mtime_nsec ||
        (unsigned)sb.st_mode != mode) {
        if (errcap) snprintf(err, errcap, "source changed during replacement: %s",
                             source_real);
        return GLON_IO_STALE;
    }
    if (rename(temp_path, resolved) != 0)
        return fail(err, errcap, "rename", resolved);
    return GLON_IO_OK;
}

int glon_io_posix_unlink(const char *path, char *err, size_t errcap) {
    if (unlink(path) == 0 || errno == ENOENT) return GLON_IO_OK;
    return fail(err, errcap, "unlink", path);
}
