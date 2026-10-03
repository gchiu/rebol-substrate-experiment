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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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

/* ShellExecuteW with a result check.  Returns 0 on success, -1 if Windows
 * rejected the request ((INT_PTR)result <= 32).  Logs the failure code. */
static int shell_open(const wchar_t *arg) {
    HINSTANCE r = ShellExecuteW(NULL, L"open", arg, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        fprintf(stderr, "glon-desktop: ShellExecuteW failed (code %lld)\n", (long long)(INT_PTR)r);
        return -1;
    }
    return 0;
}

/* Quote one argument into a Windows command line (defined with the spawn
 * machinery below). */
static void wappend_quoted(wchar_t *buf, int *n, int cap, const wchar_t *s);

/* Open a folder by explicitly launching the trusted executable explorer.exe
 * with the normalized absolute directory as a separate argument:
 *
 *     explorer.exe "<absolute directory>"
 *
 * This is a direct CreateProcessW call (no cmd.exe, no shell command string);
 * argv is quoted with the normal Windows rules so spaces are preserved.  The
 * executable is fixed by the host and cannot be chosen by the application or
 * browser.  Returns 0 if the process was created, -1 otherwise.  A created
 * process is NOT proof of a visible window. */
static int run_explorer(const wchar_t *abs) {
    wchar_t windir[MAX_PATH];
    if (GetSystemWindowsDirectoryW(windir, MAX_PATH) == 0) {
        fprintf(stderr, "glon-desktop: GetSystemWindowsDirectoryW failed (error %lu)\n", GetLastError());
        return -1;
    }
    wchar_t exe[MAX_PATH];
    if (_snwprintf(exe, MAX_PATH, L"%ls\\explorer.exe", windir) < 0) return -1;

    wchar_t cmd[32768];
    int cap = (int)(sizeof cmd / sizeof cmd[0]);
    int cn = 0;
    wappend_quoted(cmd, &cn, cap, exe);            /* argv[0]: explorer.exe */
    if (cn < cap - 1) cmd[cn++] = L' ';
    wappend_quoted(cmd, &cn, cap, abs);            /* argv[1]: the directory */
    cmd[cn] = 0;

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;   /* normal, visible interactive window */

    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (!ok) {
        fprintf(stderr, "glon-desktop: CreateProcessW(explorer.exe) failed (error %lu)\n", GetLastError());
        return -1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    fprintf(stderr, "glon-desktop: launched explorer.exe %ls\n", cmd);
    return 0;
}

void glon_open_browser(const char *url) {
    int n = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    if (n <= 0) return;
    wchar_t *wide = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!wide) return;
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, n);
    shell_open(wide);
    free(wide);
}

int glon_open_path(const char *path) {
    /* Resolve to a proper absolute Windows path before opening it; a relative
     * path with forward slashes is not reliably accepted by ShellExecuteW. */
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (n <= 0) { fprintf(stderr, "glon-desktop: cannot convert path '%s'\n", path); return -1; }
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return -1;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);

    DWORD need = GetFullPathNameW(w, 0, NULL, NULL);
    if (need == 0) {
        fprintf(stderr, "glon-desktop: GetFullPathNameW failed for '%s' (error %lu)\n", path, GetLastError());
        free(w);
        return -1;
    }
    wchar_t *full = (wchar_t *)malloc((size_t)(need + 1) * sizeof(wchar_t));
    if (!full) { free(w); return -1; }
    DWORD got = GetFullPathNameW(w, need + 1, full, NULL);
    free(w);
    if (got == 0 || got > need) {
        fprintf(stderr, "glon-desktop: GetFullPathNameW failed for '%s' (error %lu)\n", path, GetLastError());
        free(full);
        return -1;
    }

    int rc = run_explorer(full);
    if (rc != 0)
        fprintf(stderr, "glon-desktop: could not launch explorer for folder '%ls'\n", full);
    else
        fprintf(stderr, "glon-desktop: explorer.exe launched for folder '%ls' (process created; visibility not asserted)\n", full);
    free(full);
    return rc;
}

int glon_mkdirs(const char *path) {
    char tmp[1024];
    if (!path || strlen(path) >= sizeof tmp) return -1;
    strcpy(tmp, path);
    for (int i = 1; tmp[i]; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char save = tmp[i];
            tmp[i] = 0;
            CreateDirectoryA(tmp, NULL);
            tmp[i] = save;
        }
    }
    CreateDirectoryA(tmp, NULL);
    return 0;
}

/* Append one argument to a Windows command line, quoted and backslash-escaped
 * so it survives CreateProcess intact.  This is argument passing, not a shell. */
static void wappend_quoted(wchar_t *buf, int *n, int cap, const wchar_t *s) {
    #define PUT(ch) do { if (*n < cap - 1) buf[(*n)++] = (wchar_t)(ch); } while (0)
    PUT(L'"');
    int bs = 0;
    for (const wchar_t *p = s; *p; p++) {
        if (*p == L'\\') { bs++; continue; }
        if (*p == L'"') {
            for (int i = 0; i < 2 * bs + 1; i++) PUT(L'\\');
            PUT(L'"');
            bs = 0;
        } else {
            for (int i = 0; i < bs; i++) PUT(L'\\');
            PUT(*p);
            bs = 0;
        }
    }
    for (int i = 0; i < 2 * bs; i++) PUT(L'\\');
    PUT(L'"');
    #undef PUT
}

int glon_spawn_stream(const char *exe, char *const argv[],
                      int (*cb)(const char *record, int len, void *user),
                      void *user, int *exit_code) {
    (void)exe;
    int cap = 32768;
    wchar_t *cmd = (wchar_t *)malloc((size_t)cap * sizeof(wchar_t));
    if (!cmd) return -1;
    int cn = 0;
    for (int i = 0; argv[i]; i++) {
        int need = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, NULL, 0);
        wchar_t *w = (wchar_t *)malloc((size_t)(need > 0 ? need : 1) * sizeof(wchar_t));
        if (!w) { free(cmd); return -1; }
        MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, w, need);
        if (i) { if (cn < cap - 1) cmd[cn++] = L' '; }
        wappend_quoted(cmd, &cn, cap, w);
        free(w);
    }
    cmd[cn] = 0;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { free(cmd); return -1; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = NULL;

    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmd);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return -1; }

    char buf[4096];
    char rec[8192];
    int rlen = 0;
    int cancelled = 0;
    DWORD got = 0;
    while (!cancelled && ReadFile(rd, buf, sizeof buf, &got, NULL) && got > 0) {
        for (DWORD i = 0; i < got; i++) {
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
    CloseHandle(rd);
    if (cancelled) TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (exit_code) *exit_code = cancelled ? -1 : (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
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
    char *argv[5];
    argv[0] = (char *)"certutil";
    argv[1] = (char *)"-hashfile";
    argv[2] = (char *)path;
    argv[3] = (char *)"SHA256";
    argv[4] = NULL;
    char acc[8192];
    struct sha_capture c = { acc, 0, (int)sizeof acc };
    acc[0] = 0;
    int code = 0;
    if (glon_spawn_stream("certutil", argv, sha_cb, &c, &code) != 0) return -1;
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
