/* desktop/glon_host_posix.c -- POSIX implementation of the desktop OS boundary.
 *
 * TCP with BSD sockets; process/open with fork + execlp (xdg-open / open).
 * Kept so the desktop proof still builds and runs on Linux/WSL unchanged.
 */

#define _GNU_SOURCE
#include "glon_host.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
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

void glon_open_browser(const char *url) {
    const char *candidates[] = { "xdg-open", "open", NULL };
    for (int i = 0; candidates[i]; i++) {
        pid_t p = fork();
        if (p == 0) {
            execlp(candidates[i], candidates[i], url, (char *)0);
            _exit(127);
        }
        if (p > 0) return;
    }
    fprintf(stderr, "glon-desktop: no browser opener found (xdg-open/open)\n");
}
