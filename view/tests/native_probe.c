#include "gv_backend.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <richedit.h>
#endif

#ifdef _WIN32

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int main(void) {
    if (!gv_native_available()) {
        printf("SKIP: native controls require Windows\n");
        return 0;
    }
    HINSTANCE inst = GetModuleHandleW(NULL);
    HWND parent = CreateWindowExW(0, L"STATIC", L"Glon/View native probe",
                                  WS_OVERLAPPED | WS_VISIBLE, 0, 0, 480, 320,
                                  NULL, NULL, inst, NULL);
    CHECK(parent != NULL);
    if (!parent) return 1;

    gv_rect edit_rect = {20, 20, 420, 28};
    void *edit = gv_native_control_create(parent, 0, edit_rect, "hello");
    CHECK(edit != NULL);
    char buf[256];
    if (edit) {
        memset(buf, 0, sizeof buf);
        CHECK(gv_native_control_get_text(edit, buf, sizeof buf) == 0);
        CHECK(strcmp(buf, "hello") == 0);
        CHECK(gv_native_control_set_text(edit, "Glon/View") == 0);
        memset(buf, 0, sizeof buf);
        CHECK(gv_native_control_get_text(edit, buf, sizeof buf) == 0);
        CHECK(strcmp(buf, "Glon/View") == 0);
        CHECK(gv_native_control_set_focus(edit, 1) == 0);
        gv_native_control_destroy(edit);
    }

    gv_rect rich_rect = {20, 70, 420, 180};
    void *rich = gv_native_control_create(parent, 2, rich_rect,
                                         "line one\r\nline two");
    CHECK(rich != NULL);
    if (rich) {
        memset(buf, 0, sizeof buf);
        CHECK(gv_native_control_get_text(rich, buf, sizeof buf) == 0);
        CHECK(strlen(buf) > 0);
        CHECK(gv_native_control_set_text(rich, "Glon rich text") == 0);
        memset(buf, 0, sizeof buf);
        CHECK(gv_native_control_get_text(rich, buf, sizeof buf) == 0);
        CHECK(strcmp(buf, "Glon rich text") == 0);
        gv_native_control_destroy(rich);
    }

    DestroyWindow(parent);
    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: Win32 native text controls (EDIT + Rich Edit 4.1)\n");
    return 0;
}

#else

int main(void) {
    printf("SKIP: native controls require Windows (available=%d)\n",
           gv_native_available());
    return 0;
}

#endif