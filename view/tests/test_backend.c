#include "gv_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static const uint8_t gv_test_image[4 * 2 * 2] = {
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 255, 255,
};

static void make_test_list(gv_display_list *dl) {
    gv_cmd c;
    gv_display_list_init(dl);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_PUSH;
    CHECK(gv_display_list_add(dl, &c) == 0);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_TRANSFORM;
    c.transform.a = 1.0;
    c.transform.d = 1.0;
    c.transform.e = 10.0;
    c.transform.f = 20.0;
    CHECK(gv_display_list_add(dl, &c) == 0);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_RECT;
    c.rect.x = 0;
    c.rect.y = 0;
    c.rect.w = 120;
    c.rect.h = 40;
    c.color.r = 40;
    c.color.g = 90;
    c.color.b = 180;
    c.color.a = 255;
    CHECK(gv_display_list_add(dl, &c) == 0);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_TEXT;
    c.rect.x = 8;
    c.rect.y = 8;
    c.text = "GV-1";
    c.text_len = 4;
    c.f1 = 16.0;
    c.color.r = 240;
    c.color.g = 240;
    c.color.b = 240;
    c.color.a = 255;
    CHECK(gv_display_list_add(dl, &c) == 0);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_LINE;
    c.rect.x = 0;
    c.rect.y = 50;
    c.rect.w = 120;
    c.rect.h = 0;
    c.f1 = 2.0;
    c.color.r = 220;
    c.color.g = 60;
    c.color.b = 60;
    c.color.a = 255;
    CHECK(gv_display_list_add(dl, &c) == 0);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_CLIP;
    c.rect.x = 0;
    c.rect.y = 0;
    c.rect.w = 200;
    c.rect.h = 80;
    CHECK(gv_display_list_add(dl, &c) == 0);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_IMAGE;
    c.rect.x = 130;
    c.rect.y = 5;
    c.rect.w = 20;
    c.rect.h = 20;
    c.pixels = gv_test_image;
    c.image_w = 2;
    c.image_h = 2;
    CHECK(gv_display_list_add(dl, &c) == 0);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_POP;
    CHECK(gv_display_list_add(dl, &c) == 0);
}

static void test_display_list(void) {
    gv_display_list dl;
    make_test_list(&dl);
    CHECK(dl.count == 8);
    CHECK(!dl.overflow);
    gv_display_list_free(&dl);
    CHECK(dl.count == 0);
    CHECK(dl.cmds == NULL);
}

static void test_headless(void) {
    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 640;
    cfg.height = 480;
    cfg.title = "headless";
    cfg.headless = 1;

    gv_backend *a = gv_backend_headless();
    CHECK(a != NULL);
    CHECK(a->init(a, &cfg) == 0);
    CHECK(a->create_window(a, 640, 480, "headless") == 0);
    CHECK(a->has_cap(a, GV_CAP_WINDOW));
    CHECK(a->has_cap(a, GV_CAP_HEADLESS));
    CHECK(!a->has_cap(a, GV_CAP_NATIVE_CONTROLS));
    CHECK(!a->has_cap(a, GV_CAP_TSF));
    CHECK(a->native_handle(a) == NULL);

    gv_display_list dl;
    make_test_list(&dl);
    CHECK(a->begin_frame(a) == 0);
    CHECK(a->draw(a, &dl) == 0);
    CHECK(a->end_frame(a) == 0);
    uint64_t h1 = gv_backend_headless_hash(a);
    CHECK(h1 != 0);
    gv_display_list_free(&dl);

    gv_backend *b = gv_backend_headless();
    CHECK(b != NULL);
    CHECK(b->init(b, &cfg) == 0);
    CHECK(b->create_window(b, 640, 480, "headless") == 0);
    make_test_list(&dl);
    CHECK(b->begin_frame(b) == 0);
    CHECK(b->draw(b, &dl) == 0);
    CHECK(b->end_frame(b) == 0);
    uint64_t h2 = gv_backend_headless_hash(b);
    gv_display_list_free(&dl);
    CHECK(h1 == h2);

    CHECK(b->resize_window(b, 800, 600) == 0);
    make_test_list(&dl);
    CHECK(b->begin_frame(b) == 0);
    CHECK(b->draw(b, &dl) == 0);
    CHECK(b->end_frame(b) == 0);
    uint64_t h3 = gv_backend_headless_hash(b);
    gv_display_list_free(&dl);
    CHECK(h3 != h1);

    gv_event ev;
    CHECK(b->poll_event(b, &ev) == 0);
    CHECK(ev.type == GV_EVENT_NONE);

    void *ctrl = NULL;
    CHECK(b->native_control_create(b, 0, (gv_rect){0, 0, 100, 20}, "x", &ctrl) ==
          -1);
    char buf[8];
    CHECK(b->get_clipboard(b, buf, sizeof buf) == -1);

    b->destroy(b);
    a->destroy(a);
}

static int file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return (int)n;
}

static void test_sdl(void) {
    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 320;
    cfg.height = 200;
    cfg.title = "GV-1 SDL";
    cfg.headless = 1;

    gv_backend *b = gv_backend_sdl();
    CHECK(b != NULL);
    if (!b) return;
    if (b->init(b, &cfg) != 0) {
        fprintf(stderr, "SDL backend unavailable; skipping window test\n");
        b->destroy(b);
        return;
    }
    CHECK(b->create_window(b, 320, 200, "GV-1 SDL") == 0);
    CHECK(b->has_cap(b, GV_CAP_WINDOW));
    CHECK(b->has_cap(b, GV_CAP_CLIPBOARD));
    CHECK(b->has_cap(b, GV_CAP_TEXT_SHAPE));
    CHECK(!b->has_cap(b, GV_CAP_NATIVE_CONTROLS));
    CHECK(!b->has_cap(b, GV_CAP_TSF));

    gv_display_list dl;
    make_test_list(&dl);
    CHECK(b->begin_frame(b) == 0);
    CHECK(b->draw(b, &dl) == 0);
    CHECK(b->end_frame(b) == 0);
    int rc = gv_backend_sdl_save_png(b, "evidence/gv1-sdl.png");
    CHECK(rc == 0);
    int n = file_size("evidence/gv1-sdl.png");
    CHECK(n > 0);
    fprintf(stderr, "sdl: evidence/gv1-sdl.png bytes=%d\n", n);
    gv_display_list_free(&dl);

    gv_event ev;
    CHECK(b->poll_event(b, &ev) == 0);
    CHECK(ev.type == GV_EVENT_NONE);

    b->destroy(b);
}

static void test_native(void) {
    int available = gv_native_available();
    fprintf(stderr, "native: available=%d\n", available);
#if defined(_WIN32)
    CHECK(available == 1);
#else
    CHECK(available == 0);
#endif
}

int main(void) {
    test_display_list();
    test_headless();
    test_sdl();
    test_native();
    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: GV-1 backend spike tests\n");
    return 0;
}