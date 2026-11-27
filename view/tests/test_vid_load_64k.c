#include "gv_glon_host.h"
#include "gv_view.h"
#include "gv_backend.h"
#include "r0_s1.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : ".";
    gv_view *v = gv_view_new(400, 300);
    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 400;
    cfg.height = 300;
    cfg.headless = 1;
    gv_backend *b = gv_backend_headless();
    if (!v || !b || b->init(b, &cfg) != 0) return 2;
    gv_glon_host *h = gv_glon_host_new(v, b);
    if (!h || gv_glon_host_load_vid_app(h, root, "experiments/gui-f1b/empty_app.glon") != 0) {
        fprintf(stderr, "64K VID load failed\n");
        return 1;
    }
    cell globals = 0, cap = 0;
    r0_s1_global_usage(&globals, &cap);
    printf("64K_VID_LOAD PASS globals=%ld/%ld loader=%ld\\n",
           (long)globals, (long)cap, (long)M[GC_LOADER_HP]);
    return 0;
}
