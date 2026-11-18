/* c_copy.c - matched host-side copy benchmarks for IO-4.
 *
 *   c_copy MODE SIZE ITERATIONS
 *   MODE = native | service | transport
 *
 * native    POSIX read/write only
 * service   glon_io_service request/reply service, direct calls
 * transport glon_io_cli worker submission/wait, no Glon evaluator
 *
 * The source is a temporary root containing `in`; `out` is produced.  This is
 * a benchmark only, not part of the Glon runtime or its published ABI.
 */
#define _POSIX_C_SOURCE 200809L
#include "glon_io_file.h"
#include "glon_io_file_cli.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static size_t make_request(uint8_t *p, uint8_t op, uint32_t id,
                           const void *f0, uint32_t n0,
                           const void *f1, uint32_t n1, int nf) {
    p[0] = GLON_IO_REQUEST_MAGIC;
    p[1] = op;
    glon_io_put_u32(p + 2, id);
    p[6] = (uint8_t)nf;
    size_t off = 7;
    const void *fs[2] = { f0, f1 };
    uint32_t ns[2] = { n0, n1 };
    for (int i = 0; i < nf; i++) {
        glon_io_put_u32(p + off, ns[i]); off += 4;
        memcpy(p + off, fs[i], ns[i]); off += ns[i];
    }
    return off;
}

static int get_reply_field(const uint8_t *frame, size_t len, unsigned index,
                           const uint8_t **bytes, uint32_t *flen) {
    if (len < 7 || frame[0] != GLON_IO_REPLY_MAGIC) return -1;
    unsigned count = frame[6];
    if (index >= count) return -1;
    size_t off = 7;
    for (unsigned i = 0; i < count; i++) {
        if (off + 4 > len) return -1;
        uint32_t n = glon_io_get_u32(frame + off); off += 4;
        if (off + n > len) return -1;
        if (i == index) { *bytes = frame + off; *flen = n; return 0; }
        off += n;
    }
    return -1;
}

static int copy_native(const char *in, const char *out, size_t size) {
    uint8_t buf[400];
    int fi = open(in, O_RDONLY);
    int fo = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fi < 0 || fo < 0) return -1;
    size_t left = size;
    while (left) {
        size_t want = left < sizeof buf ? left : sizeof buf;
        ssize_t n = read(fi, buf, want);
        if (n <= 0) { close(fi); close(fo); return -1; }
        size_t off = 0;
        while (off < (size_t)n) {
            ssize_t w = write(fo, buf + off, (size_t)n - off);
            if (w <= 0) { close(fi); close(fo); return -1; }
            off += (size_t)w;
        }
        left -= (size_t)n;
    }
    close(fi); close(fo); return 0;
}

static int execute(glon_io_service *svc, const uint8_t *req, size_t rlen,
                   glon_io_reply *rep) {
    glon_io_request q;
    char err[128];
    if (glon_io_decode_request(req, rlen, &q, err, sizeof err) != 0) return -1;
    if (glon_io_execute(svc, &q, rep, err, sizeof err) != GLON_IO_OK) return -1;
    return 0;
}

static int copy_service(glon_io_service *svc, size_t size) {
    uint8_t req[GLON_IO_MAX_FRAME], hf[8];
    glon_io_reply rep;
    uint32_t id = 1, hv = 0, hg = 0;
    const char *in = "in";
    uint8_t mode = GLON_IO_MODE_READ;
    size_t rlen = make_request(req, GLON_IO_OP_OPEN, id++, in, 2, &mode, 1, 2);
    if (execute(svc, req, rlen, &rep) || rep.status != GLON_IO_OK ||
        rep.field_count != 2) return -1;
    hv = glon_io_get_u32(rep.fields[0].bytes);
    hg = glon_io_get_u32(rep.fields[1].bytes);
    glon_io_put_u32(hf, hv); glon_io_put_u32(hf + 4, hg);

    uint8_t modew = GLON_IO_MODE_WRITE;
    rlen = make_request(req, GLON_IO_OP_OPEN, id++, "out", 3, &modew, 1, 2);
    if (execute(svc, req, rlen, &rep) || rep.status != GLON_IO_OK ||
        rep.field_count != 2) return -1;
    uint8_t of[8];
    uint32_t ov = glon_io_get_u32(rep.fields[0].bytes);
    uint32_t og = glon_io_get_u32(rep.fields[1].bytes);
    glon_io_put_u32(of, ov); glon_io_put_u32(of + 4, og);

    uint8_t mf[4];
    size_t left = size;
    while (left) {
        uint32_t want = left < 400 ? (uint32_t)left : 400;
        glon_io_put_u32(mf, want);
        rlen = make_request(req, GLON_IO_OP_READ, id++, hf, 8, mf, 4, 2);
        if (execute(svc, req, rlen, &rep) || rep.status != GLON_IO_OK ||
            rep.field_count != 1) return -1;
        const uint8_t *data = rep.fields[0].bytes;
        uint32_t n = rep.fields[0].len;
        rlen = make_request(req, GLON_IO_OP_WRITE, id++, of, 8, data, n, 2);
        if (execute(svc, req, rlen, &rep) || rep.status != GLON_IO_OK)
            return -1;
        left -= n;
    }
    rlen = make_request(req, GLON_IO_OP_CLOSE, id++, hf, 8, NULL, 0, 1);
    if (execute(svc, req, rlen, &rep) || rep.status != GLON_IO_OK) return -1;
    rlen = make_request(req, GLON_IO_OP_CLOSE, id++, of, 8, NULL, 0, 1);
    if (execute(svc, req, rlen, &rep) || rep.status != GLON_IO_OK) return -1;
    return 0;
}

static int wait_transport(glon_io_cli *cli, uint8_t *req, size_t rlen,
                          uint8_t *rep, size_t *rplen, uint32_t expect) {
    char err[256];
    if (glon_io_cli_submit(cli, req, rlen, err, sizeof err) != GLON_IO_OK) return -1;
    if (glon_io_cli_wait(cli, rep, GLON_IO_MAX_FRAME, rplen, err, sizeof err) != 0)
        return -1;
    if (*rplen < 7 || rep[0] != GLON_IO_REPLY_MAGIC ||
        glon_io_get_u32(rep + 1) != expect || rep[5] != GLON_IO_OK) return -1;
    return 0;
}

static int copy_transport(glon_io_cli *cli, size_t size) {
    uint8_t req[GLON_IO_MAX_FRAME], rep[GLON_IO_MAX_FRAME];
    size_t rplen = 0;
    uint32_t id = 1, hv = 0, hg = 0, ov = 0, og = 0;
    const uint8_t *rp;
    uint32_t rn;
    uint8_t mode = GLON_IO_MODE_READ;
    size_t rlen = make_request(req, GLON_IO_OP_OPEN, id++, "in", 2, &mode, 1, 2);
    if (wait_transport(cli, req, rlen, rep, &rplen, 1) ||
        get_reply_field(rep, rplen, 0, &rp, &rn) || rn != 4) return -1;
    hv = glon_io_get_u32(rp);
    if (get_reply_field(rep, rplen, 1, &rp, &rn) || rn != 4) return -1;
    hg = glon_io_get_u32(rp);
    uint8_t hf[8], of[8]; glon_io_put_u32(hf, hv); glon_io_put_u32(hf + 4, hg);

    uint8_t modew = GLON_IO_MODE_WRITE;
    rlen = make_request(req, GLON_IO_OP_OPEN, id++, "out", 3, &modew, 1, 2);
    if (wait_transport(cli, req, rlen, rep, &rplen, id - 1) ||
        get_reply_field(rep, rplen, 0, &rp, &rn) || rn != 4) return -1;
    ov = glon_io_get_u32(rp);
    if (get_reply_field(rep, rplen, 1, &rp, &rn) || rn != 4) return -1;
    og = glon_io_get_u32(rp);
    glon_io_put_u32(of, ov); glon_io_put_u32(of + 4, og);

    uint8_t mf[4];
    size_t left = size;
    while (left) {
        uint32_t want = left < 400 ? (uint32_t)left : 400;
        glon_io_put_u32(mf, want);
        rlen = make_request(req, GLON_IO_OP_READ, id++, hf, 8, mf, 4, 2);
        if (wait_transport(cli, req, rlen, rep, &rplen, id - 1) ||
            get_reply_field(rep, rplen, 0, &rp, &rn)) return -1;
        rlen = make_request(req, GLON_IO_OP_WRITE, id++, of, 8, rp, rn, 2);
        if (wait_transport(cli, req, rlen, rep, &rplen, id - 1)) return -1;
        left -= rn;
    }
    rlen = make_request(req, GLON_IO_OP_CLOSE, id++, hf, 8, NULL, 0, 1);
    if (wait_transport(cli, req, rlen, rep, &rplen, id - 1)) return -1;
    rlen = make_request(req, GLON_IO_OP_CLOSE, id++, of, 8, NULL, 0, 1);
    if (wait_transport(cli, req, rlen, rep, &rplen, id - 1)) return -1;
    return 0;
}

static int check_file(const char *root, size_t size) {
    char p[4096]; snprintf(p, sizeof p, "%s/out", root);
    struct stat st;
    if (stat(p, &st) != 0 || (size_t)st.st_size != size) return -1;
    int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;
    uint8_t buf[4096];
    size_t left = size;
    while (left) {
        size_t want = left < sizeof buf ? left : sizeof buf;
        ssize_t n = read(fd, buf, want);
        if (n <= 0) { close(fd); return -1; }
        for (ssize_t i = 0; i < n; i++)
            if (buf[i] != (uint8_t)'x') { close(fd); return -1; }
        left -= (size_t)n;
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: c_copy native|service|transport SIZE ITERATIONS\n");
        return 2;
    }
    const char *mode = argv[1];
    size_t size = (size_t)strtoull(argv[2], NULL, 10);
    int iters = (int)strtol(argv[3], NULL, 10);
    if (!size || iters < 1) return 2;
    char tmpl[] = "/tmp/glon-io4-copy.XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) { perror("mkdtemp"); return 2; }
    char in[4096], out[4096];
    snprintf(in, sizeof in, "%s/in", root);
    snprintf(out, sizeof out, "%s/out", root);
    int fd = open(in, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 2;
    uint8_t buf[400]; memset(buf, 'x', sizeof buf);
    size_t left = size;
    while (left) {
        size_t n = left < sizeof buf ? left : sizeof buf;
        if (write(fd, buf, n) != (ssize_t)n) return 2;
        left -= n;
    }
    close(fd);

    int rc = 0;
    glon_io_policy p; glon_io_policy_default(&p);
    p.root = root; p.allow_read = 1; p.allow_write = 1;
    glon_io_service svc;
    glon_io_cli *cli = NULL;
    char err[256];
    if (!strcmp(mode, "service")) {
        if (glon_io_service_init(&svc, &p, err, sizeof err)) rc = 2;
    } else if (!strcmp(mode, "transport")) {
        if (glon_io_cli_init(&cli, &p, err, sizeof err)) rc = 2;
    } else if (strcmp(mode, "native")) {
        fprintf(stderr, "bad mode\n"); rc = 2;
    }
    double t0 = now_ms();
    for (int k = 0; k < iters && !rc; k++) {
        int r;
        if (!strcmp(mode, "native")) r = copy_native(in, out, size);
        else if (!strcmp(mode, "service")) r = copy_service(&svc, size);
        else r = copy_transport(cli, size);
        if (r) rc = 1;
    }
    double elapsed = now_ms() - t0;
    if (!rc && check_file(root, size)) rc = 1;
    printf("mode=%s size=%zu iters=%d elapsed_ms=%.6f bytes_per_s=%.0f\n",
           mode, size, iters, elapsed,
           elapsed > 0 ? (double)size * iters * 1000.0 / elapsed : 0.0);
    if (cli) glon_io_cli_destroy(cli);
    else if (!strcmp(mode, "service")) glon_io_service_destroy(&svc);
    unlink(in); unlink(out); rmdir(root);
    return rc;
}
