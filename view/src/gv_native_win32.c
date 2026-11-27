#include "gv_backend.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <richedit.h>
#include <stdio.h>
#endif

#ifdef _WIN32

typedef struct gv_native_control {
    HWND hwnd;
    int kind;
} gv_native_control;

static wchar_t *gv_utf8_to_wide(const char *s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) != n) {
        free(w);
        return NULL;
    }
    return w;
}

static char *gv_wide_to_utf8(const wchar_t *w) {
    if (!w) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)malloc((size_t)n);
    if (!s) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) != n) {
        free(s);
        return NULL;
    }
    return s;
}

int gv_native_available(void) {
    return 1;
}

void *gv_native_control_create(void *parent, int kind, gv_rect rect,
                               const char *text) {
    if (!parent) return NULL;
    HWND parent_hwnd = (HWND)parent;
    HINSTANCE inst = GetModuleHandleW(NULL);
    const wchar_t *cls;
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_EX_CLIENTEDGE;
    DWORD exstyle = WS_EX_CLIENTEDGE;

    if (kind == 2) {
        if (!LoadLibraryW(L"Msftedit.dll")) return NULL;
        cls = MSFTEDIT_CLASS;
        style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL;
    } else {
        cls = L"EDIT";
        if (kind == 1) {
            style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
        } else {
            style |= ES_AUTOHSCROLL;
        }
    }

    HWND hwnd = CreateWindowExW(exstyle, cls, L"", style,
                                rect.x, rect.y, rect.w, rect.h,
                                parent_hwnd, NULL, inst, NULL);
    if (!hwnd) return NULL;
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT),
                 TRUE);
    if (text && text[0]) {
        wchar_t *w = gv_utf8_to_wide(text);
        if (w) {
            SetWindowTextW(hwnd, w);
            free(w);
        }
    }
    gv_native_control *ctrl = (gv_native_control *)calloc(1, sizeof(*ctrl));
    if (!ctrl) {
        DestroyWindow(hwnd);
        return NULL;
    }
    ctrl->hwnd = hwnd;
    ctrl->kind = kind;
    return ctrl;
}

void gv_native_control_destroy(void *ctrl) {
    if (!ctrl) return;
    gv_native_control *c = (gv_native_control *)ctrl;
    if (c->hwnd) DestroyWindow(c->hwnd);
    free(c);
}

int gv_native_control_set_text(void *ctrl, const char *text) {
    if (!ctrl) return -1;
    gv_native_control *c = (gv_native_control *)ctrl;
    wchar_t *w = gv_utf8_to_wide(text ? text : "");
    if (!w) return -1;
    BOOL ok = SetWindowTextW(c->hwnd, w);
    free(w);
    return ok ? 0 : -1;
}

int gv_native_control_get_text(void *ctrl, char *buf, int cap) {
    if (!ctrl || !buf || cap <= 0) return -1;
    gv_native_control *c = (gv_native_control *)ctrl;
    int len = GetWindowTextLengthW(c->hwnd);
    if (len < 0) return -1;
    wchar_t *w = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
    if (!w) return -1;
    int got = GetWindowTextW(c->hwnd, w, len + 1);
    if (got < 0) {
        free(w);
        return -1;
    }
    char *s = gv_wide_to_utf8(w);
    free(w);
    if (!s) return -1;
    int n = snprintf(buf, (size_t)cap, "%s", s);
    free(s);
    return n >= cap ? -1 : 0;
}

int gv_native_control_set_focus(void *ctrl, int focus) {
    if (!ctrl) return -1;
    gv_native_control *c = (gv_native_control *)ctrl;
    if (focus) {
        SetFocus(c->hwnd);
    } else {
        SetFocus(GetParent(c->hwnd));
    }
    return 0;
}

#else

int gv_native_available(void) {
    return 0;
}

void *gv_native_control_create(void *parent, int kind, gv_rect rect,
                               const char *text) {
    (void)parent; (void)kind; (void)rect; (void)text;
    return NULL;
}

void gv_native_control_destroy(void *ctrl) {
    (void)ctrl;
}

int gv_native_control_set_text(void *ctrl, const char *text) {
    (void)ctrl; (void)text;
    return -1;
}

int gv_native_control_get_text(void *ctrl, char *buf, int cap) {
    (void)ctrl; (void)buf; (void)cap;
    return -1;
}

int gv_native_control_set_focus(void *ctrl, int focus) {
    (void)ctrl; (void)focus;
    return -1;
}

#endif