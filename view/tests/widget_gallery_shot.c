#include "gv_glon_host.h"
#include "r0_s1.h"
#include "gv_view.h"
#include "gv_backend.h"

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

static int gid_at(gv_view *v, gv_glon_host *h, int x, int y) {
    gv_node_id id = 0;
    int lx = 0, ly = 0;
    if (!gv_view_hit_test(v, x, y, &id, &lx, &ly) || !id) return 0;
    return gv_glon_host_glon_id(h, id);
}

int main(int argc, char **argv) {
    const char *root = ".";
    const char *app = NULL;
    const char *screenshot = NULL;
    int width = 1200, height = 700;
    int click_x = -1, click_y = -1;
    const char *click_text = NULL;
    int click_gid = 0;
    int click_theme = 0;
    int click_table = 0;
    int dump_nodes = 0;
    int dump_focus = 0;
    int gui = 0;
    int gui3 = 0;
    int gc = 0;
    int resize_w = 0, resize_h = 0;
    int down_x = -1, down_y = -1;
    int move_x = -1, move_y = -1;
    int up_x = -1, up_y = -1;
    int cancel_x = -1, cancel_y = -1;
    const char *key_list = NULL;
    const char *key_mod_list = NULL;
    const char *char_text = NULL;
    const char *char_after_text = NULL;
    const char *clipboard_set = NULL;
    int dump_clipboard = 0;
    int wheel_x = -1, wheel_y = -1, wheel_dx = 0, wheel_dy = 0;
    const char *run_source = NULL;
    const char *run_before = NULL;
    int hit_x = -1, hit_y = -1;
    const char *pointer_list = NULL;
    const char *click_list = NULL;
    const char *click_text_list = NULL;
    const char *type_text = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--app") && i + 1 < argc) app = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshot = argv[++i];
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) width = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) height = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--click-x") && i + 1 < argc) click_x = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--click-y") && i + 1 < argc) click_y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--click-text") && i + 1 < argc) click_text = argv[++i];
        else if (!strcmp(argv[i], "--click-gid") && i + 1 < argc) click_gid = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--click-theme")) click_theme = 1;
        else if (!strcmp(argv[i], "--click-table")) click_table = 1;
        else if (!strcmp(argv[i], "--dump-nodes")) dump_nodes = 1;
        else if (!strcmp(argv[i], "--dump-focus")) dump_focus = 1;
        else if (!strcmp(argv[i], "--resize") && i + 2 < argc) {
            resize_w = atoi(argv[++i]);
            resize_h = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--key-list") && i + 1 < argc) key_list = argv[++i];
        else if (!strcmp(argv[i], "--key-mod-list") && i + 1 < argc) key_mod_list = argv[++i];
        else if (!strcmp(argv[i], "--char") && i + 1 < argc) char_text = argv[++i];
        else if (!strcmp(argv[i], "--clipboard-set") && i + 1 < argc) clipboard_set = argv[++i];
        else if (!strcmp(argv[i], "--run") && i + 1 < argc) run_source = argv[++i];
        else if (!strcmp(argv[i], "--run-before") && i + 1 < argc) run_before = argv[++i];
        else if (!strcmp(argv[i], "--hit") && i + 2 < argc) {
            hit_x = atoi(argv[++i]);
            hit_y = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--wheel") && i + 4 < argc) {
            wheel_x = atoi(argv[++i]);
            wheel_y = atoi(argv[++i]);
            wheel_dx = atoi(argv[++i]);
            wheel_dy = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--dump-clipboard")) dump_clipboard = 1;
        else if (!strcmp(argv[i], "--char-after") && i + 1 < argc) char_after_text = argv[++i];
        else if (!strcmp(argv[i], "--pointer-down-x") && i + 2 < argc) {
            down_x = atoi(argv[++i]); down_y = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--pointer-move-x") && i + 2 < argc) {
            move_x = atoi(argv[++i]); move_y = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--pointer-up-x") && i + 2 < argc) {
            up_x = atoi(argv[++i]); up_y = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--pointer-cancel-x") && i + 2 < argc) {
            cancel_x = atoi(argv[++i]); cancel_y = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--gui")) gui = 1;
        else if (!strcmp(argv[i], "--gui3")) gui3 = 1;
        else if (!strcmp(argv[i], "--gc")) gc = 1;
        else if (!strcmp(argv[i], "--pointer-list") && i + 1 < argc) pointer_list = argv[++i];
        else if (!strcmp(argv[i], "--click-list") && i + 1 < argc) click_list = argv[++i];
        else if (!strcmp(argv[i], "--click-text-list") && i + 1 < argc) click_text_list = argv[++i];
        else if (!strcmp(argv[i], "--type") && i + 1 < argc) type_text = argv[++i];
    }
    if (!app || !screenshot) {
        fprintf(stderr, "usage: widget_gallery_shot --app APP --screenshot PNG [--click-x X --click-y Y]\n");
        return 2;
    }

    gv_view *v = gv_view_new(width, height);
    if (!v) return 1;
    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = width;
    cfg.height = height;
    cfg.title = "Glon Widget Gallery";
    cfg.headless = 0;
    gv_backend *b = gv_backend_sdl();
    if (!b || b->init(b, &cfg) != 0 || b->create_window(b, width, height, cfg.title) != 0) {
        fprintf(stderr, "SDL/Cairo unavailable\n");
        gv_view_free(v);
        return 1;
    }

    if (clipboard_set && b->set_clipboard)
        b->set_clipboard(b, clipboard_set);

    gv_glon_host *h = gv_glon_host_new(v, b);
    if (!h || (gui3 ? gv_glon_host_load_gui3_app(h, root, app)
                    : gui ? gv_glon_host_load_gui_app(h, root, app)
                          : gv_glon_host_load_machine_vid_app(h, root, app)) != 0 ||
        gv_glon_host_build_app(h, NULL) != 0 || gv_glon_host_bind(h) != 0 ||
        gv_glon_host_error(h)) {
        fprintf(stderr, "Glon gallery failed to start\n");
        gv_glon_host_free(h);
        b->destroy(b);
        gv_view_free(v);
        return 1;
    }
    if (render(h, b) != 0) return 1;
    {
        cell globals = 0, cap = 0;
        r0_s1_global_usage(&globals, &cap);
        fprintf(stderr, "[globals] %ld/%ld\n", (long)globals, (long)cap);
        fprintf(stderr, "[loader] high=%ld\n", (long)M[GC_LOADER_HP]);
        fprintf(stderr,
                "[heap] high=%ld gc_count=%ld live=%ld free=%ld reclaimed=%ld\n",
                (long)r0_s1_heap_high(), (long)r0_s1_gc_count(),
                (long)r0_s1_gc_live_cells(), (long)r0_s1_gc_free_cells(),
                (long)r0_s1_gc_reclaimed());
    }

    if (resize_w > 0 && resize_h > 0) {
        if (gv_glon_host_resize(h, resize_w, resize_h) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "resize failed\n");
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    if (click_text_list) {
        char *copy = malloc(strlen(click_text_list) + 1);
        if (!copy) return 1;
        strcpy(copy, click_text_list);
        char *save = NULL;
        for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            gv_node_id id = 0;
            for (int i = 0; i < v->cap; i++) {
                gv_node *n = &v->nodes[i];
                if (n->type == GV_NODE_TEXT && n->text && !strcmp(n->text, tok)) {
                    id = n->parent;
                    break;
                }
            }
            if (!id) {
                fprintf(stderr, "click text %s missed\n", tok);
                free(copy);
                return 1;
            }
            int gid = gv_glon_host_glon_id(h, id);
            /* as the runner does for a press: the old focus is blurred and
             * the pressed widget focused before its click (GUI-P2: the
             * gallery's field takes typing only while focused) */
            gv_node_id old = gv_view_focus_id(v);
            int old_gid = old ? gv_glon_host_glon_id(h, old) : 0;
            if (gid && old_gid && old_gid != gid) gv_glon_host_blur(h, old_gid);
            if (gid && gv_view_focus(v, id) == 0) gv_glon_host_focus(h, gid);
            if (!gid || gv_glon_host_click(h, gid) != 0 || gv_glon_host_error(h)) {
                fprintf(stderr, "click text %s failed gid=%d\n", tok, gid);
                free(copy);
                return 1;
            }
            if (render(h, b) != 0) { free(copy); return 1; }
        }
        free(copy);
    }

    if (click_list) {
        char *copy = malloc(strlen(click_list) + 1);
        if (!copy) return 1;
        strcpy(copy, click_list);
        char *save = NULL;
        for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            int x = atoi(tok);
            tok = strtok_r(NULL, ",", &save);
            if (!tok) break;
            int y = atoi(tok);
            gv_node_id id = 0;
            int lx = 0, ly = 0;
            if (!gv_view_hit_test(v, x, y, &id, &lx, &ly) || !id) {
                fprintf(stderr, "click at %d,%d missed\n", x, y);
                free(copy);
                return 1;
            }
            int gid = gv_glon_host_glon_id(h, id);
            if (!gid || gv_glon_host_click_xy(h, gid, x, y) != 0 || gv_glon_host_error(h)) {
                fprintf(stderr, "click at %d,%d failed gid=%d\n", x, y, gid);
                free(copy);
                return 1;
            }
            if (render(h, b) != 0) { free(copy); return 1; }
        }
        free(copy);
    }

    if (click_table) {
        char buf[64];
        if (gv_glon_host_text(h, "gallery/table-id", buf, sizeof buf) != 0) {
            fprintf(stderr, "cannot read gallery/table-id\n");
            return 1;
        }
        click_gid = atoi(buf);
        if (!click_gid) {
            fprintf(stderr, "table gid not found\n");
            return 1;
        }
    }
    if (click_theme) {
        char buf[64];
        if (gv_glon_host_text(h, "gallery/theme-id", buf, sizeof buf) != 0) {
            fprintf(stderr, "cannot read gallery/theme-id\n");
            return 1;
        }
        click_gid = atoi(buf);
    }
    if (down_x >= 0 && down_y >= 0) {
        int gid = gid_at(v, h, down_x, down_y);
        if (!gid || gv_glon_host_pointer_down(h, gid, down_x, down_y) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "pointer down at %d,%d failed gid=%d\n", down_x, down_y, gid);
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }
    if (move_x >= 0 && move_y >= 0) {
        int gid = gid_at(v, h, move_x, move_y);
        if (!gid || gv_glon_host_pointer_move(h, gid) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "pointer move at %d,%d failed gid=%d\n", move_x, move_y, gid);
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }
    if (up_x >= 0 && up_y >= 0) {
        int gid = gid_at(v, h, up_x, up_y);
        if (!gid || gv_glon_host_pointer_up(h, gid, up_x, up_y) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "pointer up at %d,%d failed gid=%d\n", up_x, up_y, gid);
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }
    if (cancel_x >= 0 && cancel_y >= 0) {
        int gid = gid_at(v, h, cancel_x, cancel_y);
        if (!gid || gv_glon_host_pointer_cancel(h, gid) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "pointer cancel at %d,%d failed gid=%d\n", cancel_x, cancel_y, gid);
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    if (pointer_list) {
        char *copy = malloc(strlen(pointer_list) + 1);
        if (!copy) return 1;
        strcpy(copy, pointer_list);
        char *save = NULL;
        for (char *tok = strtok_r(copy, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
            char *colon = strchr(tok, ':');
            if (!colon) continue;
            *colon = 0;
            char *coords = colon + 1;
            char *comma = strchr(coords, ',');
            if (!comma) continue;
            *comma = 0;
            int x = atoi(coords);
            int y = atoi(comma + 1);
            int gid = gid_at(v, h, x, y);
            if (!gid) {
                fprintf(stderr, "pointer-list %s at %d,%d missed\n", tok, x, y);
                free(copy);
                return 1;
            }
            int rc = 0;
            if (!strcmp(tok, "down")) rc = gv_glon_host_pointer_down(h, gid, x, y);
            else if (!strcmp(tok, "up")) rc = gv_glon_host_pointer_up(h, gid, x, y);
            else if (!strcmp(tok, "move")) rc = gv_glon_host_pointer_move(h, gid);
            else if (!strcmp(tok, "cancel")) rc = gv_glon_host_pointer_cancel(h, gid);
            else { fprintf(stderr, "pointer-list bad op %s\n", tok); free(copy); return 1; }
            if (rc != 0 || gv_glon_host_error(h)) {
                fprintf(stderr, "pointer-list %s at %d,%d failed gid=%d\n", tok, x, y, gid);
                free(copy);
                return 1;
            }
            if (render(h, b) != 0) { free(copy); return 1; }
        }
        free(copy);
    }

    if (wheel_x >= 0 && wheel_y >= 0) {
        if (gv_glon_host_wheel(h, wheel_x, wheel_y, wheel_dx, wheel_dy) != 0 ||
            gv_glon_host_error(h)) {
            fprintf(stderr, "wheel at %d,%d failed\n", wheel_x, wheel_y);
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    if (run_source) {
        if (gv_glon_host_run(h, run_source) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "run source failed\n");
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    if (hit_x >= 0 && hit_y >= 0) {
        int gid = gid_at(v, h, hit_x, hit_y);
        fprintf(stderr, "hit=%d\n", gid);
    }

    if (click_gid || click_text || (click_x >= 0 && click_y >= 0)) {
        gv_node_id id = 0;
        int lx = 0, ly = 0;
        if (click_gid) {
            id = gv_glon_host_view_node(h, click_gid);
            if (!id) {
                fprintf(stderr, "click gid %d missed\n", click_gid);
                return 1;
            }
        } else if (click_text) {
            for (int i = 0; i < v->cap; i++) {
                gv_node *n = &v->nodes[i];
                if (n->type == GV_NODE_TEXT && n->text && !strcmp(n->text, click_text)) {
                    id = n->parent;
                    break;
                }
            }
            if (!id) {
                fprintf(stderr, "click text %s missed\n", click_text);
                return 1;
            }
        } else if (!gv_view_hit_test(v, click_x, click_y, &id, &lx, &ly) || !id) {
            fprintf(stderr, "click at %d,%d missed\n", click_x, click_y);
            return 1;
        }
        int gid = click_gid ? click_gid : gv_glon_host_glon_id(h, id);
        if (!gid || gv_glon_host_click_xy(h, gid, click_x, click_y) != 0 || gv_glon_host_error(h)) {
            cell t = 0, i = 0, a = 0;
            int has = r0_s1_uncaught_error(&t, &i, &a);
            char ts[128] = "", is[128] = "", as[128] = "";
            if (has) {
                r0_s1_mold(t, ts, sizeof ts);
                r0_s1_mold(i, is, sizeof is);
                r0_s1_mold(a, as, sizeof as);
            }
            fprintf(stderr, "click at %d,%d failed gid=%d sin=%d %s/%s/%s halt=%d clean=%d ip=%ld..%ld sp=%ld..%ld rp=%ld..%ld sentry=%d\n",
                    click_x, click_y, gid, has, ts, is, as, r0_s1_halt_reason(),
                    r0_s1_ran_cleanly(), (long)r0_s1_ip_start(), (long)r0_s1_ip_end(),
                    (long)r0_s1_sp_start(), (long)r0_s1_sp_end(),
                    (long)r0_s1_rp_start(), (long)r0_s1_rp_end(),
                    r0_s1_stack_sentry_fired());
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    if (char_text) {
        if (gv_glon_host_char_event(h, char_text) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "char failed\n");
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }
    if (gc) {
        if (gv_glon_host_run(h, "gc/collect") != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "gc failed\n");
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    if (run_before) {
        if (gv_glon_host_run(h, run_before) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "run-before failed\n");
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    if (key_list) {
        char *copy = malloc(strlen(key_list) + 1);
        if (!copy) return 1;
        strcpy(copy, key_list);
        char *save = NULL;
        for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            int key = atoi(tok);
            if (gv_glon_host_key(h, key) != 0 || gv_glon_host_error(h)) {
                fprintf(stderr, "key %d failed\n", key);
                free(copy);
                return 1;
            }
            if (render(h, b) != 0) { free(copy); return 1; }
        }
        free(copy);
    }

    if (key_mod_list) {
        char *copy = malloc(strlen(key_mod_list) + 1);
        if (!copy) return 1;
        strcpy(copy, key_mod_list);
        char *save = NULL;
        for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            char *colon = strchr(tok, ':');
            if (!colon) continue;
            *colon = 0;
            int key = atoi(tok);
            unsigned int mods = (unsigned int)strtoul(colon + 1, NULL, 10);
            if (gv_glon_host_key_mods(h, key, mods) != 0 || gv_glon_host_error(h)) {
                fprintf(stderr, "key-mod %d/%u failed\n", key, mods);
                free(copy);
                return 1;
            }
            if (render(h, b) != 0) { free(copy); return 1; }
        }
        free(copy);
    }

    if (dump_clipboard) {
        char cb[4096];
        int rc = (b->get_clipboard) ? b->get_clipboard(b, cb, sizeof cb) : -1;
        fprintf(stderr, "clipboard=%s\n", (rc == 0 && cb[0]) ? cb : "<empty>");
    }

    if (char_after_text) {
        if (gv_glon_host_char_event(h, char_after_text) != 0 || gv_glon_host_error(h)) {
            fprintf(stderr, "char-after failed\n");
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    if (type_text) {
        /* as the SDL runner does: the host input buffer and a view-char */
        if (gv_glon_host_char(h, type_text) != 0 || gv_glon_host_char_event(h, type_text) != 0 ||
            gv_glon_host_error(h)) {
            fprintf(stderr, "type failed\n");
            return 1;
        }
        if (render(h, b) != 0) return 1;
    }

    {
        int takes = 0, chunks = 0, ferrs = 0;
        gv_glon_host_f1b_stats(h, &takes, &chunks, &ferrs);
        fprintf(stderr, "[f1b] takes=%d chunks=%d errors=%d\n", takes, chunks, ferrs);
    }

    if (dump_nodes) {
        for (int i = 0; i < v->cap; i++) {
            gv_node *n = &v->nodes[i];
            if (n->type)
                fprintf(stderr, "node %d type=%d parent=%d rect=%d,%d %dx%d vis=%d text=%s\n",
                        i, n->type, n->parent, n->x, n->y, n->w, n->h, n->visible,
                        n->text ? n->text : "");
        }
    }

    if (dump_focus) {
        gv_node_id fv = gv_view_focus_id(v);
        fprintf(stderr, "focus view=%d glon=%d\n", (int)fv,
                fv ? gv_glon_host_glon_id(h, fv) : 0);
    }

    if (gv_backend_sdl_save_png(b, screenshot) != 0) return 1;
    printf("wrote %s\n", screenshot);
    gv_glon_host_free(h);
    b->destroy(b);
    gv_view_free(v);
    return 0;
}