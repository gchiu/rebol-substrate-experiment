/* glon_io_file_windows.c - native Win32 path confinement and byte backend.
 *
 * Logical names are UTF-8, relative, and use '/' as the separator.  The root
 * is canonicalised with GetFinalPathNameByHandleW().  Parent directories are
 * canonicalised before every operation; reparse points (symlinks, junctions)
 * are rejected on the final component.  Handles are Win32 HANDLEs stored in
 * the opaque glon_io_fd.
 *
 * The public Glon vocabulary and the request/reply protocol are unchanged.
 * File permissions/ACLs are enforced by Windows; POSIX mode bits are not
 * meaningful and are reported as a platform difference.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include "glon_io_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define IO_PATH_CAP 4096
#define WIN_PATH_CAP 32768

static int fail_win(const char *op, const char *path, char *err, size_t errcap) {
    DWORD e = GetLastError();
    int st = GLON_IO_IO_ERROR;
    switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        st = GLON_IO_NOT_FOUND; break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_CANT_ACCESS_FILE:
        st = GLON_IO_PERMISSION; break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
    case ERROR_FILE_TOO_LARGE:
    case ERROR_FILENAME_EXCED_RANGE:
        st = GLON_IO_TOO_LARGE; break;
    case ERROR_INVALID_HANDLE:
        st = GLON_IO_INVALID_HANDLE; break;
    default:
        st = GLON_IO_IO_ERROR; break;
    }
    if (errcap) snprintf(err, errcap, "%s: %s (win32 %lu)", op, path, (unsigned long)e);
    return st;
}

static int utf8_to_wide(const char *s, int len, WCHAR *out, size_t outcap) {
    if (len < 0) len = (int)strlen(s);
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, NULL, 0);
    if (n <= 0 || (size_t)n >= outcap) return -1;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, out, n) != n)
        return -1;
    out[n] = 0;
    return 0;
}

static int wide_to_utf8(const WCHAR *ws, char *out, size_t outcap) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ws, -1, NULL, 0, NULL, NULL);
    if (n <= 0 || (size_t)n > outcap) return -1;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ws, -1, out, n, NULL, NULL) != n)
        return -1;
    return 0;
}

static int under_root(const WCHAR *root, const WCHAR *path) {
    size_t n = wcslen(root);
    if (n == 0) return 0;
    if (_wcsnicmp(path, root, n) != 0) return 0;
    if (path[n] == 0) return 1;
    if (path[n] == L'\\') return 1;
    if (root[n - 1] == L'\\' && path[n - 1] == 0) return 1;
    return 0;
}

/* The existing logical-name rule is deliberately strict: no drive letters,
 * no UNC prefixes, no backslashes, no empty/./.. components, no ADS colons.
 * Windows adds reserved device names, trailing dots/spaces, and invalid
 * filename characters to the same rejection. */
static int component_reserved(const char *p, size_t n) {
    char base[16];
    size_t b = 0;
    while (b < n && b + 1 < sizeof base && p[b] != '.') {
        base[b] = (char)tolower((unsigned char)p[b]);
        b++;
    }
    base[b] = 0;
    if (!strcmp(base, "con") || !strcmp(base, "prn") || !strcmp(base, "aux") ||
        !strcmp(base, "nul") || !strcmp(base, "conin$") || !strcmp(base, "conout$"))
        return 1;
    if (b == 4 && (!strncmp(base, "com", 3) || !strncmp(base, "lpt", 3)) &&
        base[3] >= '1' && base[3] <= '9')
        return 1;
    return 0;
}

static int name_ok(const char *name, size_t len) {
    if (len == 0 || len >= IO_PATH_CAP) return 0;
    if (name[0] == '/' || name[0] == '\\' || name[0] == ':') return 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || name[i] == '/') {
            size_t part = i - start;
            if (part == 0) return 0;
            if (part == 1 && name[start] == '.') return 0;
            if (part == 2 && name[start] == '.' && name[start + 1] == '.') return 0;
            if (name[i - 1] == '.' || name[i - 1] == ' ') return 0;
            if (component_reserved(name + start, part)) return 0;
            start = i + 1;
        } else if (name[i] == '\\' || name[i] == ':' || name[i] == 0) {
            return 0;
        } else if ((unsigned char)name[i] < 32 || name[i] == '<' || name[i] == '>' ||
                   name[i] == '"' || name[i] == '|' || name[i] == '?' ||
                   name[i] == '*') {
            return 0;
        }
    }
    return 1;
}

static int join_wide(const WCHAR *root, const WCHAR *name, WCHAR *out, size_t cap) {
    size_t rn = wcslen(root), nn = wcslen(name);
    int slash = (rn > 0 && root[rn - 1] != L'\\');
    if (rn + (size_t)slash + nn + 1 > cap) return -1;
    wcscpy(out, root);
    if (slash) wcscat(out, L"\\");
    wcscat(out, name);
    return 0;
}

static int open_dir_final(const WCHAR *path, WCHAR *out, size_t cap,
                          char *err, size_t errcap) {
    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return fail_win("open-dir", "", err, errcap);
    DWORD n = GetFinalPathNameByHandleW(h, out, (DWORD)cap, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (n == 0 || n >= cap) {
        if (errcap) snprintf(err, errcap, "canonical path too long");
        return GLON_IO_TOO_LARGE;
    }
    return GLON_IO_OK;
}

static int resolve_existing(const WCHAR *root, const WCHAR *joined,
                            WCHAR *out, size_t cap, char *err, size_t errcap) {
    HANDLE h = CreateFileW(joined, FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return fail_win("open", "", err, errcap);
    BY_HANDLE_FILE_INFORMATION bi;
    if (!GetFileInformationByHandle(h, &bi)) {
        CloseHandle(h);
        return fail_win("stat", "", err, errcap);
    }
    if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        CloseHandle(h);
        if (errcap) snprintf(err, errcap, "reparse/symlink source rejected");
        return GLON_IO_PERMISSION;
    }
    DWORD n = GetFinalPathNameByHandleW(h, out, (DWORD)cap, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (n == 0 || n >= cap) {
        if (errcap) snprintf(err, errcap, "canonical path too long");
        return GLON_IO_TOO_LARGE;
    }
    if (!under_root(root, out)) {
        if (errcap) snprintf(err, errcap, "path escapes io root");
        return GLON_IO_PERMISSION;
    }
    return GLON_IO_OK;
}

static int resolve_parent(const WCHAR *root, const WCHAR *joined,
                          WCHAR *parent_out, size_t parent_cap,
                          WCHAR *target_out, size_t target_cap,
                          char *err, size_t errcap) {
    WCHAR buf[WIN_PATH_CAP];
    if (wcslen(joined) + 1 > WIN_PATH_CAP) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    wcscpy(buf, joined);
    WCHAR *slash = wcsrchr(buf, L'\\');
    if (!slash) {
        if (errcap) snprintf(err, errcap, "invalid path");
        return GLON_IO_IO_ERROR;
    }
    WCHAR *base = slash + 1;
    if (slash == buf) slash[1] = 0;
    else *slash = 0;
    int st = open_dir_final(buf, parent_out, parent_cap, err, errcap);
    if (st != GLON_IO_OK) return st;
    if (!under_root(root, parent_out)) {
        if (errcap) snprintf(err, errcap, "path escapes io root");
        return GLON_IO_PERMISSION;
    }
    if (wcslen(parent_out) + 1 + wcslen(base) + 1 > target_cap) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    wcscpy(target_out, parent_out);
    if (target_out[wcslen(target_out) - 1] != L'\\') wcscat(target_out, L"\\");
    wcscat(target_out, base);
    return GLON_IO_OK;
}

int glon_io_backend_root(const char *root, char *out, size_t outcap,
                         char *err, size_t errcap) {
    WCHAR wroot[WIN_PATH_CAP], final[WIN_PATH_CAP];
    if (utf8_to_wide(root, -1, wroot, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "io root is not valid UTF-8");
        return GLON_IO_PERMISSION;
    }
    DWORD attr = GetFileAttributesW(wroot);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (errcap) snprintf(err, errcap, "io root is not a directory: %s", root);
        return GLON_IO_NOT_FOUND;
    }
    int st = open_dir_final(wroot, final, WIN_PATH_CAP, err, errcap);
    if (st != GLON_IO_OK) return st;
    if (wide_to_utf8(final, out, outcap) != 0) {
        if (errcap) snprintf(err, errcap, "io root path too long");
        return GLON_IO_TOO_LARGE;
    }
    return GLON_IO_OK;
}

int glon_io_backend_resolve_path(const char *root_real, const char *name,
                                   size_t name_len, int create,
                                   char *out, size_t outcap,
                                   char *err, size_t errcap) {
    if (!name_ok(name, name_len)) {
        if (errcap) snprintf(err, errcap, "invalid logical file name");
        return GLON_IO_PERMISSION;
    }
    WCHAR wroot[WIN_PATH_CAP], wname[WIN_PATH_CAP], joined[WIN_PATH_CAP];
    WCHAR parent[WIN_PATH_CAP], target[WIN_PATH_CAP];
    if (utf8_to_wide(root_real, -1, wroot, WIN_PATH_CAP) != 0 ||
        utf8_to_wide(name, (int)name_len, wname, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "path is not valid UTF-8");
        return GLON_IO_PERMISSION;
    }
    if (join_wide(wroot, wname, joined, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    int st = create
        ? resolve_parent(wroot, joined, parent, WIN_PATH_CAP,
                         target, WIN_PATH_CAP, err, errcap)
        : resolve_existing(wroot, joined, target, WIN_PATH_CAP, err, errcap);
    if (st != GLON_IO_OK) return st;
    if (wide_to_utf8(target, out, outcap) != 0) {
        if (errcap) snprintf(err, errcap, "resolved path too long");
        return GLON_IO_TOO_LARGE;
    }
    return GLON_IO_OK;
}

int glon_io_backend_open(const char *root_real, const char *name, size_t name_len,
                         int mode, glon_io_fd *fd, char *err, size_t errcap) {
    *fd = GLON_IO_INVALID_FD;
    if (!name_ok(name, name_len)) {
        if (errcap) snprintf(err, errcap, "invalid logical file name");
        return GLON_IO_PERMISSION;
    }
    WCHAR wroot[WIN_PATH_CAP], wname[WIN_PATH_CAP], joined[WIN_PATH_CAP];
    WCHAR parent[WIN_PATH_CAP], target[WIN_PATH_CAP];
    if (utf8_to_wide(root_real, -1, wroot, WIN_PATH_CAP) != 0 ||
        utf8_to_wide(name, (int)name_len, wname, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "path is not valid UTF-8");
        return GLON_IO_PERMISSION;
    }
    if (join_wide(wroot, wname, joined, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    int st = resolve_parent(wroot, joined, parent, WIN_PATH_CAP,
                            target, WIN_PATH_CAP, err, errcap);
    if (st != GLON_IO_OK) return st;

    DWORD access;
    DWORD disposition;
    if (mode == GLON_IO_MODE_READ) {
        access = GENERIC_READ; disposition = OPEN_EXISTING;
    } else if (mode == GLON_IO_MODE_READ_WRITE) {
        access = GENERIC_READ | GENERIC_WRITE; disposition = OPEN_EXISTING;
    } else if (mode == GLON_IO_MODE_APPEND) {
        access = FILE_APPEND_DATA; disposition = OPEN_ALWAYS;
    } else {
        access = GENERIC_WRITE; disposition = CREATE_ALWAYS;
    }
    DWORD attr = GetFileAttributesW(target);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (errcap) snprintf(err, errcap, "reparse/symlink target rejected");
        return GLON_IO_PERMISSION;
    }
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (errcap) snprintf(err, errcap, "target is a directory");
        return GLON_IO_UNSUPPORTED;
    }
    HANDLE h = CreateFileW(target, access,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, disposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return fail_win("open", "", err, errcap);
    BY_HANDLE_FILE_INFORMATION bi;
    if (!GetFileInformationByHandle(h, &bi)) {
        CloseHandle(h);
        return fail_win("stat", "", err, errcap);
    }
    if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        CloseHandle(h);
        if (errcap) snprintf(err, errcap, "reparse/symlink target rejected");
        return GLON_IO_PERMISSION;
    }
    *fd = (glon_io_fd)h;
    return GLON_IO_OK;
}

int glon_io_backend_read(glon_io_fd fd, uint8_t *buf, size_t cap, size_t *got,
                         char *err, size_t errcap) {
    *got = 0;
    DWORD want = (DWORD)(cap > 0x7fffffffu ? 0x7fffffffu : cap);
    DWORD n = 0;
    if (!ReadFile((HANDLE)fd, buf, want, &n, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_HANDLE_EOF) return GLON_IO_OK;
        SetLastError(e);
        return fail_win("read", "", err, errcap);
    }
    *got = n;
    return GLON_IO_OK;
}

int glon_io_backend_write(glon_io_fd fd, const uint8_t *buf, size_t len,
                          size_t *put, char *err, size_t errcap) {
    *put = 0;
    DWORD want = (DWORD)(len > 0x7fffffffu ? 0x7fffffffu : len);
    DWORD n = 0;
    if (!WriteFile((HANDLE)fd, buf, want, &n, NULL))
        return fail_win("write", "", err, errcap);
    *put = n;
    return GLON_IO_OK;
}

int glon_io_backend_close(glon_io_fd fd, char *err, size_t errcap) {
    if (!CloseHandle((HANDLE)fd)) return fail_win("close", "", err, errcap);
    return GLON_IO_OK;
}

int glon_io_backend_set_mode(glon_io_fd fd, unsigned mode,
                             char *err, size_t errcap) {
    (void)fd; (void)mode; (void)err; (void)errcap;
    return GLON_IO_OK; /* Windows ACLs/attributes are the authority. */
}

static unsigned long long file_index(const BY_HANDLE_FILE_INFORMATION *bi) {
    return ((unsigned long long)bi->nFileIndexHigh << 32) | bi->nFileIndexLow;
}

static long long file_time(const FILETIME *ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft->dwLowDateTime;
    u.HighPart = ft->dwHighDateTime;
    return (long long)u.QuadPart;
}

int glon_io_backend_make_temp(const char *root_real, const char *name,
                              size_t name_len, glon_io_fd *fd,
                              char *temp_path, size_t temp_cap,
                              char *source_real, size_t source_cap,
                              char *parent_real, size_t parent_cap,
                              unsigned long long *dev, unsigned long long *ino,
                              unsigned long long *size, long long *mtime_sec,
                              long long *mtime_nsec, unsigned *mode,
                              char *err, size_t errcap) {
    *fd = GLON_IO_INVALID_FD;
    if (!name_ok(name, name_len)) {
        if (errcap) snprintf(err, errcap, "invalid logical file name");
        return GLON_IO_PERMISSION;
    }
    WCHAR wroot[WIN_PATH_CAP], wname[WIN_PATH_CAP], joined[WIN_PATH_CAP];
    WCHAR source[WIN_PATH_CAP], parent[WIN_PATH_CAP], target[WIN_PATH_CAP];
    if (utf8_to_wide(root_real, -1, wroot, WIN_PATH_CAP) != 0 ||
        utf8_to_wide(name, (int)name_len, wname, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "path is not valid UTF-8");
        return GLON_IO_PERMISSION;
    }
    if (join_wide(wroot, wname, joined, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    int st = resolve_existing(wroot, joined, source, WIN_PATH_CAP, err, errcap);
    if (st != GLON_IO_OK) return st;
    HANDLE sh = CreateFileW(source, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (sh == INVALID_HANDLE_VALUE) return fail_win("open-source", "", err, errcap);
    BY_HANDLE_FILE_INFORMATION bi;
    if (!GetFileInformationByHandle(sh, &bi)) {
        CloseHandle(sh);
        return fail_win("stat-source", "", err, errcap);
    }
    if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        CloseHandle(sh);
        if (errcap) snprintf(err, errcap, "reparse/symlink source rejected");
        return GLON_IO_PERMISSION;
    }
    if (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        CloseHandle(sh);
        if (errcap) snprintf(err, errcap, "not a regular file");
        return GLON_IO_UNSUPPORTED;
    }
    WCHAR *slash = wcsrchr(source, L'\\');
    if (!slash) {
        CloseHandle(sh);
        if (errcap) snprintf(err, errcap, "invalid source path");
        return GLON_IO_IO_ERROR;
    }
    if (slash == source) slash[1] = 0; else *slash = 0;
    wcscpy(parent, source);

    DWORD pid = GetCurrentProcessId();
    HANDLE th = INVALID_HANDLE_VALUE;
    WCHAR temp[WIN_PATH_CAP];
    for (unsigned attempt = 0; attempt < 64; attempt++) {
        unsigned long long r = ((unsigned long long)GetTickCount64() << 20) ^
                               ((unsigned long long)pid << 8) ^ attempt ^ (unsigned long long)(uintptr_t)&th;
        int tn = _snwprintf(temp, WIN_PATH_CAP, L"%s\\.glon-tmp-%lu-%llx-%llx",
                            parent, (unsigned long)pid,
                            (unsigned long long)attempt, r);
        if (tn < 0 || tn >= WIN_PATH_CAP) {
            CloseHandle(sh);
            if (errcap) snprintf(err, errcap, "temporary path too long");
            return GLON_IO_TOO_LARGE;
        }
        th = CreateFileW(temp, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (th != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
            CloseHandle(sh);
            return fail_win("temp-open", "", err, errcap);
        }
    }
    CloseHandle(sh);
    if (th == INVALID_HANDLE_VALUE) {
        if (errcap) snprintf(err, errcap, "cannot create unique temporary file");
        return GLON_IO_IO_ERROR;
    }
    if (wide_to_utf8(source, source_real, source_cap) != 0 ||
        wide_to_utf8(parent, parent_real, parent_cap) != 0 ||
        wide_to_utf8(temp, temp_path, temp_cap) != 0) {
        CloseHandle(th);
        DeleteFileW(temp);
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    *fd = (glon_io_fd)th;
    *dev = (unsigned long long)bi.dwVolumeSerialNumber;
    *ino = file_index(&bi);
    *size = ((unsigned long long)bi.nFileSizeHigh << 32) | bi.nFileSizeLow;
    *mtime_sec = file_time(&bi.ftLastWriteTime);
    *mtime_nsec = 0;
    *mode = 0;
    return GLON_IO_OK;
}

int glon_io_backend_commit(const char *root_real, const char *target_name,
                           size_t target_len, const char *temp_path,
                           const char *parent_real, const char *source_real,
                           unsigned long long dev, unsigned long long ino,
                           unsigned long long size, long long mtime_sec,
                           long long mtime_nsec, unsigned mode,
                           char *err, size_t errcap) {
    (void)mtime_nsec; (void)mode;
    if (!name_ok(target_name, target_len)) {
        if (errcap) snprintf(err, errcap, "invalid logical file name");
        return GLON_IO_PERMISSION;
    }
    WCHAR wroot[WIN_PATH_CAP], wtarget_name[WIN_PATH_CAP], joined[WIN_PATH_CAP];
    WCHAR wparent[WIN_PATH_CAP], wtarget[WIN_PATH_CAP], wtemp[WIN_PATH_CAP];
    WCHAR wstored_parent[WIN_PATH_CAP], wsource[WIN_PATH_CAP];
    if (utf8_to_wide(root_real, -1, wroot, WIN_PATH_CAP) != 0 ||
        utf8_to_wide(target_name, (int)target_len, wtarget_name, WIN_PATH_CAP) != 0 ||
        utf8_to_wide(temp_path, -1, wtemp, WIN_PATH_CAP) != 0 ||
        utf8_to_wide(parent_real, -1, wstored_parent, WIN_PATH_CAP) != 0 ||
        utf8_to_wide(source_real, -1, wsource, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "path is not valid UTF-8");
        return GLON_IO_PERMISSION;
    }
    if (join_wide(wroot, wtarget_name, joined, WIN_PATH_CAP) != 0) {
        if (errcap) snprintf(err, errcap, "path too long");
        return GLON_IO_TOO_LARGE;
    }
    int st = resolve_parent(wroot, joined, wparent, WIN_PATH_CAP,
                            wtarget, WIN_PATH_CAP, err, errcap);
    if (st != GLON_IO_OK) return st;
    if (_wcsicmp(wparent, wstored_parent) != 0) {
        if (errcap) snprintf(err, errcap, "temporary file and target must share a directory");
        return GLON_IO_IO_ERROR;
    }
    DWORD attr = GetFileAttributesW(wtarget);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (errcap) snprintf(err, errcap, "reparse/symlink target rejected");
        return GLON_IO_PERMISSION;
    }
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (errcap) snprintf(err, errcap, "not a regular target");
        return GLON_IO_UNSUPPORTED;
    }
    HANDLE sh = CreateFileW(wsource, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (sh == INVALID_HANDLE_VALUE) return fail_win("stat-source", "", err, errcap);
    BY_HANDLE_FILE_INFORMATION bi;
    if (!GetFileInformationByHandle(sh, &bi)) {
        CloseHandle(sh);
        return fail_win("stat-source", "", err, errcap);
    }
    CloseHandle(sh);
    if ((bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (unsigned long long)bi.dwVolumeSerialNumber != dev ||
        file_index(&bi) != ino ||
        (((unsigned long long)bi.nFileSizeHigh << 32) | bi.nFileSizeLow) != size ||
        file_time(&bi.ftLastWriteTime) != mtime_sec) {
        if (errcap) snprintf(err, errcap, "source changed during replacement");
        return GLON_IO_STALE;
    }
    if (!MoveFileExW(wtemp, wtarget, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return fail_win("rename", "", err, errcap);
    return GLON_IO_OK;
}

int glon_io_backend_unlink(const char *path, char *err, size_t errcap) {
    WCHAR w[WIN_PATH_CAP];
    if (utf8_to_wide(path, -1, w, WIN_PATH_CAP) != 0) return GLON_IO_PERMISSION;
    if (DeleteFileW(w)) return GLON_IO_OK;
    DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return GLON_IO_OK;
    SetLastError(e);
    return fail_win("unlink", "", err, errcap);
}
