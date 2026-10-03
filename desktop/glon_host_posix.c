/* desktop/glon_host_posix.c -- POSIX implementation of the desktop OS boundary.
 *
 * TCP with BSD sockets; process/open with fork + execlp (xdg-open / open).
 * Kept so the desktop proof still builds and runs on Linux/WSL unchanged.
 */

#define _GNU_SOURCE
#include "glon_host.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int glon_host_init(void) { return 0; }
void glon_host_shutdown(void) {}

void glon_ignore_sigpipe(void) { signal(SIGPIPE, SIG_IGN); }

long glon_process_id(void) { return (long)getpid(); }

glon_socket glon_tcp_listen(const char *addr, int port, int *bound_port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return GLON_INVALID_SOCKET;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, addr, &a.sin_addr) != 1) { close(fd); return GLON_INVALID_SOCKET; }
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return GLON_INVALID_SOCKET; }
    if (listen(fd, 8) != 0) { close(fd); return GLON_INVALID_SOCKET; }
    if (bound_port) {
        struct sockaddr_in got;
        socklen_t n = sizeof got;
        if (getsockname(fd, (struct sockaddr *)&got, &n) == 0)
            *bound_port = ntohs(got.sin_port);
    }
    return (glon_socket)fd;
}

glon_socket glon_tcp_accept(glon_socket srv) {
    int fd = accept((int)srv, NULL, NULL);
    return (fd < 0) ? GLON_INVALID_SOCKET : (glon_socket)fd;
}

int glon_tcp_read(glon_socket c, char *buf, int cap) {
    for (;;) {
        ssize_t r = read((int)c, buf, (size_t)cap);
        if (r < 0 && errno == EINTR) continue;
        return (int)r;
    }
}

int glon_tcp_write_all(glon_socket c, const void *buf, int n) {
    const char *b = buf;
    int left = n;
    while (left > 0) {
        ssize_t w = write((int)c, b, (size_t)left);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        b += w; left -= (int)w;
    }
    return 0;
}

void glon_tcp_close(glon_socket c) { close((int)c); }

static void spawn_opener(const char *arg) {
    const char *candidates[] = { "xdg-open", "open", NULL };
    for (int i = 0; candidates[i]; i++) {
        pid_t p = fork();
        if (p == 0) {
            execlp(candidates[i], candidates[i], arg, (char *)0);
            _exit(127);
        }
        if (p > 0) return;
    }
    fprintf(stderr, "glon-desktop: no opener found (xdg-open/open)\n");
}

void glon_open_browser(const char *url) { spawn_opener(url); }
void glon_open_path(const char *path) { spawn_opener(path); }

int glon_mkdirs(const char *path) {
    char tmp[1024];
    if (!path || strlen(path) >= sizeof tmp) return -1;
    strcpy(tmp, path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
    return 0;
}

/* Generic process/spawn: fork + execvp with a pipe, no shell.  Output records
 * are split on LF or CR and delivered as they arrive. */
int glon_spawn_stream(const char *exe, char *const argv[],
                      int (*cb)(const char *record, int len, void *user),
                      void *user, int *exit_code) {
    int p[2];
    if (pipe(p) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[0]);
        close(p[1]);
        execvp(exe, argv);
        _exit(127);
    }
    close(p[1]);
    char buf[4096];
    char rec[8192];
    int rlen = 0;
    int cancelled = 0;
    ssize_t n;
    while (!cancelled && (n = read(p[0], buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (rlen > 0) {
                    rec[rlen] = 0;
                    if (cb && cb(rec, rlen, user)) { cancelled = 1; break; }
                    rlen = 0;
                }
            } else if (rlen < (int)sizeof rec - 1) {
                rec[rlen++] = c;
            }
        }
    }
    if (!cancelled && rlen > 0) { rec[rlen] = 0; if (cb) cb(rec, rlen, user); }
    close(p[0]);
    if (cancelled) kill(pid, SIGTERM);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (exit_code) *exit_code = cancelled ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return cancelled ? 1 : 0;
}

struct sha_capture { char *buf; int n; int cap; };

static int sha_cb(const char *record, int len, void *user) {
    struct sha_capture *c = (struct sha_capture *)user;
    for (int i = 0; i < len && c->n < c->cap - 1; i++) c->buf[c->n++] = record[i];
    c->buf[c->n] = 0;
    return 0;
}

int glon_sha256(const char *path, char *out_hex, int cap) {
    char *argv[3];
    argv[0] = (char *)"sha256sum";
    argv[1] = (char *)path;
    argv[2] = NULL;
    char acc[8192];
    struct sha_capture c = { acc, 0, (int)sizeof acc };
    acc[0] = 0;
    int code = 0;
    if (glon_spawn_stream("sha256sum", argv, sha_cb, &c, &code) != 0) return -1;
    for (int i = 0; acc[i]; i++) {
        int j = 0;
        while (j < 64 && isxdigit((unsigned char)acc[i + j])) j++;
        if (j == 64) {
            int k = 0;
            for (; k < 64 && k < cap - 1; k++) out_hex[k] = acc[i + k];
            out_hex[k] = 0;
            return 0;
        }
    }
    return -1;
}
