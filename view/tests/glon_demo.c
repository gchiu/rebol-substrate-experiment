#include "gv_glon_host.h"
#include "gv_view.h"
#include "gv_backend.h"
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int render(gv_glon_host *h, gv_backend *b) {
    const gv_display_list *dl = gv_glon_host_display(h);
    if (!b || !dl) return -1;
    if (b->begin_frame(b) != 0) return -1;
    if (b->draw(b, dl) != 0) return -1;
    if (b->end_frame(b) != 0) return -1;
    return 0;
}

int main(int argc, char **argv) {
    int headless = 0;
    int windows = 0;      /* GLON-INST-3B: --windows */
    const char *screenshot = NULL;
    const char *root = ".";
    const char *app = NULL;
    const char *spec = NULL;
    int vid = 0;
    int vocab_vid = 0;   /* VID + V-1 vocabulary loader: the WG-3 widget gallery */
    int vid_gui3 = 0;    /* GUI-3 behaviour with VID presentation */
    int gui3 = 0;
    int width = 640, height = 400;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshot = argv[++i];
        else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc)
            root = argv[++i];
        else if (strcmp(argv[i], "--app") == 0 && i + 1 < argc)
            app = argv[++i];
        else if (strcmp(argv[i], "--spec") == 0 && i + 1 < argc)
            spec = argv[++i];
        else if (strcmp(argv[i], "--vid") == 0)
            vid = 1;
        else if (strcmp(argv[i], "--windows") == 0)    /* GLON-INST-3B: Intent's window words */
            windows = 1;
        else if (strcmp(argv[i], "--vocab-vid") == 0)
            vocab_vid = 1;
        else if (strcmp(argv[i], "--vid-gui3") == 0)
            vid_gui3 = 1;
        else if (strcmp(argv[i], "--gui3") == 0)
            gui3 = 1;
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
    }

    gv_view *v = gv_view_new(width, height);
    if (!v) return 1;

    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = width;
    cfg.height = height;
    cfg.title = "Glon/View GV-1 demonstration";
    cfg.headless = headless;

    gv_backend *b = gv_backend_sdl();
    if (!b || b->init(b, &cfg) != 0 || b->create_window(b, width, height, cfg.title) != 0) {
        fprintf(stderr, "SDL/Cairo unavailable\n");
        gv_view_free(v);
        return 1;
    }

    gv_glon_host *h = gv_glon_host_new(v, b);
    /* GLON-INST-3B: an application's own native windows are SDL windows too */
    if (h) gv_glon_host_set_window_ops(h, gv_backend_sdl_window_ops());
    if (h && windows) gv_glon_host_use_windows(h, 1);
    int load_rc = app ? (gui3 ? gv_glon_host_load_gui3_app(h, root, app)
                              : vocab_vid ? gv_glon_host_load_vocab_vid_app(h, root, app)
                              : vid_gui3 ? gv_glon_host_load_vid_gui3_app(h, root, app)
                              : vid ? gv_glon_host_load_vid_app(h, root, app)
                                    : gv_glon_host_load_app(h, root, app))
                     : gv_glon_host_load(h, root);
    int build_rc = app ? gv_glon_host_build_app(h, spec)
                       : gv_glon_host_build(h);
    if (!h || load_rc != 0 || build_rc != 0 || gv_glon_host_bind(h) != 0) {
        fprintf(stderr, "Glon application failed to start\n");
        gv_glon_host_free(h);
        b->destroy(b);
        gv_view_free(v);
        return 1;
    }
    /* GUI-P2: an interactive application learns its real window size once,
     * as it would from a resize (screenshots and headless runs keep the
     * application's own geometry, so their golden images do not change) */
    if (!screenshot && !headless && gv_glon_host_resize(h, width, height) != 0) {
        fprintf(stderr, "Glon application failed to lay out for %dx%d\n", width, height);
        return 1;
    }
    if (render(h, b) != 0) return 1;

    if (screenshot) {
        int rc = gv_backend_sdl_save_png(b, screenshot);
        gv_glon_host_free(h);
        b->destroy(b);
        gv_view_free(v);
        if (rc != 0) return 1;
        printf("wrote %s\n", screenshot);
        return 0;
    }
    if (headless) {
        gv_glon_host_free(h);
        b->destroy(b);
        gv_view_free(v);
        return 0;
    }

    int running = 1;
    Uint32 last_tick = SDL_GetTicks();
    while (running) {
        /* drain the queue, coalesce pointer moves and wheel steps, dispatch,
         * then draw once: each Glon event costs milliseconds, so replaying
         * every motion sample made the window lag behind the mouse */
        gv_event evs[256];
        int n = 0;
        while (n < 256 && b->poll_event(b, &evs[n]) > 0) n++;
        n = gv_glon_host_coalesce(evs, n);
        for (int i = 0; i < n && running; i++) {
            if (evs[i].type == GV_EVENT_QUIT ||
                (evs[i].type == GV_EVENT_KEY_DOWN && evs[i].key == 27)) {
                running = 0;
                break;
            }
            gv_glon_host_dispatch(h, &evs[i]);
        }
        /* FOREIGN-1: hosted components' own events (gv_host.h) */
        if (gv_glon_host_pump(h) > 0) n++;
        /* GLON-INST-3B: the secondary windows' events, each in its own
         * window; those that changed are drawn */
        if (running && gv_glon_host_window_count(h) > 0 && gv_glon_host_pump_windows(h) > 0) n++;
        if (n > 0 && running) render(h, b);
        if (running) gv_glon_host_render_windows(h, 0);
        Uint32 now = SDL_GetTicks();
        if (now - last_tick >= 500) {
            last_tick = now;
            gv_glon_host_tick(h);
            render(h, b);
        }
        SDL_Delay(10);
    }

    gv_glon_host_free(h);
    b->destroy(b);
    gv_view_free(v);
    printf("clean shutdown\n");
    return 0;
}