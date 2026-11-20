/* glon_io_file_tests.c - focused protocol, handle and cancellation tests.
 *
 * This links only the optional module (no R0/S1 runtime), so it can be built
 * with AddressSanitizer, UndefinedBehaviorSanitizer or ThreadSanitizer without
 * involving the interpreter's static 64K machine.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "glon_io_file.h"
#include "glon_io_file_cli.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check(int ok, const char *what) {
    if (ok) printf("PASS %s\n", what);
    else { printf("FAIL %s\n", what); failures++; }
}

static size_t put_field(uint8_t *p, size_t off, const void *data, uint32_t n) {
    glon_io_put_u32(p + off, n);
    off += 4;
    memcpy(p + off, data, n);
    return off + n;
}

static size_t make_request(uint8_t *p, uint8_t op, uint32_t id,
                           const void *f0, uint32_t n0,
                           const void *f1, uint32_t n1,
                           int fields) {
    p[0] = GLON_IO_REQUEST_MAGIC;
    p[1] = op;
    glon_io_put_u32(p + 2, id);
    p[6] = (uint8_t)fields;
    size_t off = 7;
    if (fields > 0) off = put_field(p, off, f0, n0);
    if (fields > 1) off = put_field(p, off, f1, n1);
    return off;
}

static void put_handle(uint8_t *p, uint32_t value, uint32_t generation) {
    glon_io_put_u32(p, value);
    glon_io_put_u32(p + 4, generation);
}

static int execute(glon_io_service *svc, const uint8_t *frame, size_t len,
                   glon_io_reply *rep) {
    glon_io_request req;
    char err[256];
    if (glon_io_decode_request(frame, len, &req, err, sizeof err) != 0) return -1;
    return glon_io_execute(svc, &req, rep, err, sizeof err);
}

static int has_temp(const char *root) {
    DIR *d = opendir(root);
    if (!d) return -1;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, ".glon-tmp-", 10) == 0) found = 1;
    closedir(d);
    return found;
}

static int read_file(const char *path, char *buf, size_t cap, size_t *n) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, cap);
    close(fd);
    if (r < 0) return -1;
    *n = (size_t)r;
    return 0;
}

static int make_temp_request(glon_io_service *svc, const char *name,
                             const char *dir, uint32_t id, uint32_t *hv,
                             uint32_t *hg, glon_io_reply *rep) {
    uint8_t frame[GLON_IO_MAX_FRAME];
    char err[512];
    size_t flen = make_request(frame, GLON_IO_OP_TEMP_OPEN, id, name,
                               strlen(name), NULL, 0, 1);
    (void)dir;
    int st = execute(svc, frame, flen, rep);
    if (st == GLON_IO_OK && rep->field_count == 2) {
        *hv = glon_io_get_u32(rep->fields[0].bytes);
        *hg = glon_io_get_u32(rep->fields[1].bytes);
    } else {
        *hv = *hg = 0;
    }
    (void)err;
    return st;
}

static int write_file(const char *path, const char *data, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t w = write(fd, data, n);
    close(fd);
    return w == (ssize_t)n ? 0 : -1;
}

int main(void) {
    char tmpl[] = "/tmp/glon-io-protocol.XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) { perror("mkdtemp"); return 2; }
    char path[4096];
    snprintf(path, sizeof path, "%s/doc.txt", root);
    if (write_file(path, "abcde", 5) != 0) { perror("write fixture"); return 2; }

    glon_io_policy pol;
    glon_io_policy_default(&pol);
    pol.root = root;
    pol.allow_read = 1;
    pol.allow_write = 1;
    glon_io_service svc;
    char err[512];
    check(glon_io_service_init(&svc, &pol, err, sizeof err) == 0, "service init");

    uint8_t frame[GLON_IO_MAX_FRAME], repbuf[GLON_IO_MAX_FRAME];
    glon_io_reply rep;
    const char *name = "doc.txt";
    uint8_t mode = GLON_IO_MODE_READ;
    size_t flen = make_request(frame, GLON_IO_OP_OPEN, 1, name, 7, &mode, 1, 2);
    check(execute(&svc, frame, flen, &rep) == GLON_IO_OK &&
          rep.status == GLON_IO_OK && rep.field_count == 2,
          "open request/reply");
    uint32_t hv = rep.field_count == 2 ? glon_io_get_u32(rep.fields[0].bytes) : 0;
    uint32_t hg = rep.field_count == 2 ? glon_io_get_u32(rep.fields[1].bytes) : 0;

    uint8_t hf[8], mf[4];
    put_handle(hf, hv, hg);
    glon_io_put_u32(mf, 5);
    flen = make_request(frame, GLON_IO_OP_READ, 2, hf, 8, mf, 4, 2);
    check(execute(&svc, frame, flen, &rep) == GLON_IO_OK &&
          rep.status == GLON_IO_OK && rep.fields[0].len == 5 &&
          memcmp(rep.fields[0].bytes, "abcde", 5) == 0,
          "read returns exact bytes");

    glon_io_put_u32(mf, 100);
    flen = make_request(frame, GLON_IO_OP_READ, 3, hf, 8, mf, 4, 2);
    check(execute(&svc, frame, flen, &rep) == GLON_IO_EOF &&
          rep.status == GLON_IO_EOF, "EOF status");

    flen = make_request(frame, GLON_IO_OP_CLOSE, 4, hf, 8, NULL, 0, 1);
    check(execute(&svc, frame, flen, &rep) == GLON_IO_OK &&
          rep.status == GLON_IO_OK, "close request");

    flen = make_request(frame, GLON_IO_OP_READ, 5, hf, 8, mf, 4, 2);
    check(execute(&svc, frame, flen, &rep) == GLON_IO_INVALID_HANDLE &&
          rep.status == GLON_IO_INVALID_HANDLE, "stale handle rejected");

    const char *escape = "../doc.txt";
    flen = make_request(frame, GLON_IO_OP_OPEN, 6, escape, 10, &mode, 1, 2);
    check(execute(&svc, frame, flen, &rep) == GLON_IO_PERMISSION &&
          rep.status == GLON_IO_PERMISSION, "path traversal rejected");

    /* Malformed framing must not be accepted. */
    glon_io_request req;
    uint8_t bad[64];
    memcpy(bad, frame, flen);
    bad[0] = 0x02;
    check(glon_io_decode_request(bad, flen, &req, err, sizeof err) != 0,
          "bad magic rejected");
    memcpy(bad, frame, flen);
    check(glon_io_decode_request(bad, flen - 1, &req, err, sizeof err) != 0,
          "truncated field rejected");
    memcpy(bad, frame, flen);
    bad[flen] = 0;
    check(glon_io_decode_request(bad, flen + 1, &req, err, sizeof err) != 0,
          "trailing byte rejected");
    size_t good = make_request(frame, GLON_IO_OP_OPEN, 7, name, 7, &mode, 1, 2);
    glon_io_put_u32(frame + 7, GLON_IO_MAX_FRAME + 1);
    check(glon_io_decode_request(frame, good, &req, err, sizeof err) != 0,
          "oversized field rejected");

    glon_io_policy too_big = pol;
    too_big.max_file = GLON_IO_HARD_MAX_FILE + 1;
    glon_io_service svc2;
    check(glon_io_service_init(&svc2, &too_big, err, sizeof err) != 0,
          "host hard file cap rejected");

    /* Cancellation must join the worker and never expose a stale reply. */
    glon_io_cli *cli = NULL;
    glon_io_policy slow = pol;
    slow.delay_ms = 150;
    check(glon_io_cli_init(&cli, &slow, err, sizeof err) == 0, "driver init");
    flen = make_request(frame, GLON_IO_OP_OPEN, 8, name, 7, &mode, 1, 2);
    check(glon_io_cli_submit(cli, frame, flen, err, sizeof err) == GLON_IO_OK,
          "driver submit");
    check(glon_io_cli_submit(cli, frame, flen, err, sizeof err) == GLON_IO_BUSY,
          "one outstanding request enforced");
    glon_io_cli_cancel(cli);
    size_t rlen = 0;
    check(glon_io_cli_wait(cli, repbuf, sizeof repbuf, &rlen, err, sizeof err) != 0 &&
          rlen == 0, "cancelled request yields no reply");
    check(glon_io_cli_open_handles(cli) == 0, "cancellation leaves no handle");
    glon_io_cli_destroy(cli);

    /* Oversized submit is rejected before a worker starts. */
    cli = NULL;
    check(glon_io_cli_init(&cli, &pol, err, sizeof err) == 0, "driver init 1b");
    uint8_t huge[GLON_IO_MAX_FRAME + 1] = { 0 };
    check(glon_io_cli_submit(cli, huge, sizeof huge, err, sizeof err) == GLON_IO_TOO_LARGE,
          "oversized request rejected");
    glon_io_cli_destroy(cli);

    /* A worker protocol failure must not look like success. */
    cli = NULL;
    check(glon_io_cli_init(&cli, &pol, err, sizeof err) == 0, "driver init 2");
    uint8_t junk[8] = { 0 };
    check(glon_io_cli_submit(cli, junk, sizeof junk, err, sizeof err) == GLON_IO_OK,
          "malformed submit reaches worker");
    check(glon_io_cli_wait(cli, repbuf, sizeof repbuf, &rlen, err, sizeof err) != 0,
          "worker protocol failure is not success");
    glon_io_cli_destroy(cli);

    /* Normal driver completion and cleanup. */
    cli = NULL;
    check(glon_io_cli_init(&cli, &pol, err, sizeof err) == 0, "driver init 3");
    flen = make_request(frame, GLON_IO_OP_OPEN, 9, name, 7, &mode, 1, 2);
    check(glon_io_cli_submit(cli, frame, flen, err, sizeof err) == GLON_IO_OK,
          "driver normal submit");
    check(glon_io_cli_wait(cli, repbuf, sizeof repbuf, &rlen, err, sizeof err) == 0 &&
          rlen >= 7 && repbuf[0] == GLON_IO_REPLY_MAGIC &&
          glon_io_get_u32(repbuf + 1) == 9, "driver reply id/status framed");
    check(glon_io_cli_open_handles(cli) == 1, "driver handle open");
    unsigned failed = 0;
    check(glon_io_cli_cleanup(cli, &failed) == 1 && failed == 0,
          "driver cleanup closes open handle");
    glon_io_cli_destroy(cli);


    /* ---- protocol v2: temp/commit/discard -------------------------------- */
    {
        const char *tmpname = "doc.txt";
        uint32_t thv = 0, thg = 0;
        char buf[64]; size_t got = 0;

        /* create a temp, write it, commit over the original */
        check(make_temp_request(&svc, tmpname, root, 20, &thv, &thg, &rep) == GLON_IO_OK &&
              rep.status == GLON_IO_OK && rep.field_count == 2 && has_temp(root) == 1,
              "temp-open creates exclusive temp");
        put_handle(hf, thv, thg);
        flen = make_request(frame, GLON_IO_OP_WRITE, 21, hf, 8,
                            "HELLO", 5, 2);
        check(execute(&svc, frame, flen, &rep) == GLON_IO_OK &&
              rep.status == GLON_IO_OK, "write to temp handle");
        flen = make_request(frame, GLON_IO_OP_COMMIT, 22, hf, 8,
                            "doc.txt", 7, 2);
        check(execute(&svc, frame, flen, &rep) == GLON_IO_OK &&
              rep.status == GLON_IO_OK && has_temp(root) == 0,
              "commit renames temp and removes it");
        check(read_file(path, buf, sizeof buf, &got) == 0 && got == 5 &&
              memcmp(buf, "HELLO", 5) == 0, "commit replaced original bytes");
        flen = make_request(frame, GLON_IO_OP_WRITE, 23, hf, 8, "x", 1, 2);
        check(execute(&svc, frame, flen, &rep) == GLON_IO_INVALID_HANDLE,
              "committed temp handle is invalid");

        /* discard leaves the original alone and removes the temp */
        check(write_file(path, "abcde", 5) == 0, "fixture reset");
        check(make_temp_request(&svc, tmpname, root, 24, &thv, &thg, &rep) == GLON_IO_OK &&
              has_temp(root) == 1, "temp-open for discard");
        put_handle(hf, thv, thg);
        flen = make_request(frame, GLON_IO_OP_DISCARD, 25, hf, 8, NULL, 0, 1);
        check(execute(&svc, frame, flen, &rep) == GLON_IO_OK &&
              rep.status == GLON_IO_OK && has_temp(root) == 0,
              "discard removes temp");
        check(read_file(path, buf, sizeof buf, &got) == 0 && got == 5 &&
              memcmp(buf, "abcde", 5) == 0, "discard preserves original");

        /* stale source is detected and the temp is removed */
        check(make_temp_request(&svc, tmpname, root, 26, &thv, &thg, &rep) == GLON_IO_OK,
              "temp-open for stale test");
        check(write_file(path, "changed", 7) == 0, "source modified");
        put_handle(hf, thv, thg);
        flen = make_request(frame, GLON_IO_OP_COMMIT, 27, hf, 8,
                            "doc.txt", 7, 2);
        check(execute(&svc, frame, flen, &rep) == GLON_IO_STALE &&
              rep.status == GLON_IO_STALE && has_temp(root) == 0,
              "commit rejects concurrently modified source");
        check(read_file(path, buf, sizeof buf, &got) == 0 && got == 7 &&
              memcmp(buf, "changed", 7) == 0,
              "stale commit leaves the modified source untouched");

        /* cross-directory commit is refused (and the temp removed) */
        char sub[4096];
        snprintf(sub, sizeof sub, "%s/sub", root);
        check(mkdir(sub, 0700) == 0, "subdir fixture");
        check(make_temp_request(&svc, tmpname, root, 28, &thv, &thg, &rep) == GLON_IO_OK,
              "temp-open for cross-directory test");
        put_handle(hf, thv, thg);
        flen = make_request(frame, GLON_IO_OP_COMMIT, 29, hf, 8,
                            "sub/other.txt", 13, 2);
        check(execute(&svc, frame, flen, &rep) == GLON_IO_IO_ERROR &&
              has_temp(root) == 0,
              "commit refuses a target in another directory");

        /* symlink source and symlink target are refused */
        char linkpath[4096];
        snprintf(linkpath, sizeof linkpath, "%s/link.txt", root);
        (void)unlink(linkpath);
        check(symlink("doc.txt", linkpath) == 0, "symlink fixture");
        check(make_temp_request(&svc, "link.txt", root, 30, &thv, &thg, &rep) ==
              GLON_IO_PERMISSION, "symlink source rejected");

        /* permission bits are copied (umask-independent fchmod) */
        check(chmod(path, 0751) == 0, "mode fixture");
        check(make_temp_request(&svc, tmpname, root, 31, &thv, &thg, &rep) == GLON_IO_OK,
              "temp-open for mode test");
        put_handle(hf, thv, thg);
        flen = make_request(frame, GLON_IO_OP_WRITE, 32, hf, 8, "mode", 4, 2);
        (void)execute(&svc, frame, flen, &rep);
        flen = make_request(frame, GLON_IO_OP_COMMIT, 33, hf, 8,
                            "doc.txt", 7, 2);
        check(execute(&svc, frame, flen, &rep) == GLON_IO_OK,
              "commit for mode test");
        struct stat ms;
        check(stat(path, &ms) == 0 && (ms.st_mode & 0777) == 0751,
              "commit preserves source permission bits");

        /* service cleanup removes an unfinished temp */
        check(write_file(path, "abcde", 5) == 0, "fixture reset 2");
        check(make_temp_request(&svc, tmpname, root, 34, &thv, &thg, &rep) == GLON_IO_OK &&
              has_temp(root) == 1, "temp-open for cleanup test");
        check(glon_io_service_cleanup(&svc, &failed) == 1 && has_temp(root) == 0,
              "service cleanup removes temp");
        check(read_file(path, buf, sizeof buf, &got) == 0 && got == 5 &&
              memcmp(buf, "abcde", 5) == 0, "cleanup preserves original");
    }

    glon_io_service_destroy(&svc);
    char linkpath2[4096]; snprintf(linkpath2, sizeof linkpath2, "%s/link.txt", root);
    unlink(linkpath2);
    snprintf(linkpath2, sizeof linkpath2, "%s/sub/other.txt", root);
    unlink(linkpath2);
    snprintf(linkpath2, sizeof linkpath2, "%s/sub", root);
    rmdir(linkpath2);
    unlink(path);
    rmdir(root);
    printf("%s (%d failure(s))\n", failures ? "PROTOCOL_TESTS_FAILED" : "PROTOCOL_TESTS_PASS", failures);
    return failures != 0;
}
