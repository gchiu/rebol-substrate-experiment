#include "gv_backend.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 640;
    cfg.height = 480;
    cfg.title = "headless probe";
    cfg.headless = 1;

    gv_backend *b = gv_backend_headless();
    if (!b || b->init(b, &cfg) != 0 || b->create_window(b, 640, 480, "headless") != 0) {
        return 1;
    }

    gv_display_list dl;
    gv_display_list_init(&dl);

    gv_cmd c;
    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_RECT;
    c.rect.x = 10;
    c.rect.y = 10;
    c.rect.w = 200;
    c.rect.h = 60;
    c.color.r = 70;
    c.color.g = 130;
    c.color.b = 200;
    c.color.a = 255;
    gv_display_list_add(&dl, &c);

    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_TEXT;
    c.rect.x = 20;
    c.rect.y = 20;
    c.text = "Glon/View headless";
    c.text_len = strlen(c.text);
    c.f1 = 16.0;
    c.color.r = 240;
    c.color.g = 240;
    c.color.b = 240;
    c.color.a = 255;
    gv_display_list_add(&dl, &c);

    b->begin_frame(b);
    b->draw(b, &dl);
    b->end_frame(b);

    printf("hash=%016llx\n", (unsigned long long)gv_backend_headless_hash(b));
    gv_display_list_free(&dl);
    b->destroy(b);
    return 0;
}