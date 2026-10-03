/* desktop/glon_host_windows.c -- native Windows implementation of the desktop
 * OS boundary.
 *
 * TCP with Winsock2; process/open with ShellExecuteW (the normal mechanism for
 * opening the user's default browser).  No external runtime, no WebView2, no
 * Electron.  The filesystem read is portable C stdio (glon_desktop.c).  This
 * file is the only Windows-specific code in the proof.
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <process.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glon_host.h"

int glon_host_init(void) {
    WSADATA wsa;
    return (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ? 0 : -1;
}

void glon_host_shutdown(void) { WSACleanup(); }

void glon_ignore_sigpipe(void) { /* no SIGPIPE on Windows */ }

long glon_process_id(void) { return (long)_getpid(); }

glon_socket glon_tcp_listen(const char *addr, int port, int *bound_port) {
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return GLON_INVALID_SOCKET;
    BOOL one = TRUE;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = inet_addr(addr);
    if (a.sin_addr.s_addr == INADDR_NONE) { closesocket(fd); return GLON_INVALID_SOCKET; }
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) { closesocket(fd); return GLON_INVALID_SOCKET; }
    if (listen(fd, 8) != 0) { closesocket(fd); return GLON_INVALID_SOCKET; }
    if (bound_port) {
        struct sockaddr_in got;
        int n = (int)sizeof got;
        if (getsockname(fd, (struct sockaddr *)&got, &n) == 0)
            *bound_port = ntohs(got.sin_port);
    }
    return (glon_socket)fd;
}

glon_socket glon_tcp_accept(glon_socket srv) {
    SOCKET fd = accept((SOCKET)srv, NULL, NULL);
    return (fd == INVALID_SOCKET) ? GLON_INVALID_SOCKET : (glon_socket)fd;
}

int glon_tcp_read(glon_socket c, char *buf, int cap) {
    int r = recv((SOCKET)c, buf, cap, 0);
    return (r == SOCKET_ERROR) ? -1 : r;
}

int glon_tcp_write_all(glon_socket c, const void *buf, int n) {
    const char *b = buf;
    int left = n;
    while (left > 0) {
        int w = send((SOCKET)c, b, left, 0);
        if (w == SOCKET_ERROR) return -1;
        b += w; left -= w;
    }
    return 0;
}

void glon_tcp_close(glon_socket c) { closesocket((SOCKET)c); }

void glon_open_browser(const char *url) {
    int n = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    if (n <= 0) return;
    wchar_t *wide = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!wide) return;
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, n);
    ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
    free(wide);
}
