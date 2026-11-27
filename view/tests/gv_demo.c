#include "gv_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_rect(gv_display_list *dl, int x, int y, int w, int h,
                     gv_color c) {
    gv_cmd cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.op = GV_DRAW_RECT;
    cmd.rect.x = x;
    cmd.rect.y = y;
    cmd.rect.w = w;
    cmd.rect.h = h;
    cmd.color = c;
    gv_display_list_add(dl, &cmd);
}

static void add_text(gv_display_list *dl, int x, int y, double size,
                     const char *text, gv_color c) {
    gv_cmd cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.op = GV_DRAW_TEXT;
    cmd.rect.x = x;
    cmd.rect.y = y;
    cmd.text = text;
    cmd.text_len = (uint32_t)strlen(text);
    cmd.f1 = size;
    cmd.color = c;
    gv_display_list_add(dl, &cmd);
}

int main(void) {
    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 640;
    cfg.height = 400;
    cfg.title = "Glon/View GV-1 backend spike";
    cfg.headless = 1;

    gv_backend *b = gv_backend_sdl();
    if (!b) return 1;
    if (b->init(b, &cfg) != 0) {
        fprintf(stderr, "SDL/Cairo unavailable: %s\n", "init failed");
        b->destroy(b);
        return 1;
    }
    if (b->create_window(b, 640, 400, cfg.title) != 0) {
        b->destroy(b);
        return 1;
    }

    gv_color bg = {28, 31, 36, 255};
    gv_color panel = {42, 48, 58, 255};
    gv_color accent = {70, 130, 200, 255};
    gv_color text = {235, 238, 242, 255};
    gv_color muted = {160, 168, 178, 255};

    gv_display_list dl;
    gv_display_list_init(&dl);

    add_rect(&dl, 0, 0, 640, 400, bg);
    add_text(&dl, 24, 30, 22.0, "Glon/View GV-1", text);

    add_rect(&dl, 24, 76, 360, 280, panel);
    add_text(&dl, 40, 96, 15.0, "View backend spike", muted);

    add_rect(&dl, 40, 130, 328, 44, accent);
    add_text(&dl, 56, 142, 14.0, "Button", text);

    add_rect(&dl, 40, 188, 328, 32, bg);
    add_rect(&dl, 40, 188, 328, 32, panel);
    add_text(&dl, 52, 196, 13.0, "Editable text field", muted);

    add_rect(&dl, 40, 236, 18, 18, bg);
    add_rect(&dl, 40, 236, 18, 18, accent);
    add_text(&dl, 68, 236, 13.0, "Checkbox", text);

    add_text(&dl, 400, 96, 14.0, "SDL2 + Cairo", text);
    add_text(&dl, 400, 124, 12.0, "Retained scene spike", muted);
    add_text(&dl, 400, 148, 12.0, "Headless deterministic tests", muted);

    if (b->begin_frame(b) != 0) return 1;
    if (b->draw(b, &dl) != 0) return 1;
    if (b->end_frame(b) != 0) return 1;
    int rc = gv_backend_sdl_save_png(b, "evidence/gv1-demo.png");
    gv_display_list_free(&dl);
    b->destroy(b);
    if (rc != 0) {
        fprintf(stderr, "failed to save evidence/gv1-demo.png\n");
        return 1;
    }
    printf("wrote evidence/gv1-demo.png\n");
    return 0;
}