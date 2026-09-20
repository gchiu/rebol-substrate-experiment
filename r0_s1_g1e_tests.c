/* r0_s1_g1e_tests.c - G1E demo-launcher + load-on-demand tests.
 *
 * G1E proves that ordinary Glon composition can generate a repeated family of
 * interactive controls, each carrying its own application data, while the
 * generic host bridge stays application-agnostic.
 *
 * These tests also exercise the load-on-demand split: the bootstrap
 * (demo/shop/bootstrap.glon, common.glon + app.glon bundled by build.py) is
 * parsed and run first; each demo (demo/shop/demos/*.glon) is then parsed and
 * run with r0_s1_run_persistent -- exactly the glon_load path the browser host
 * follows -- and routes/events are driven as the browser does. Comments are
 * stripped from each source exactly as the WASM glon_load does.
 */

#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char src[65536];
static char out[8192];

/* strip `;;` line comments, exactly as standalone/glon.c glon_load does */
static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

/* Load one .glon source into the persistent machine (reusing `src`, so this
 * also verifies that objects created from a prior load survive the source
 * buffer being overwritten). */
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(src, 1, sizeof src - 1, f);
    fclose(f);
    src[n] = 0;
    strip_comments(src);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) return -2;
    int N = r0_s1_run_persistent(b);
    return (N < 0) ? -3 : 0;
}

static const char *route(const char *token) {
    int out_len = 0;
    if (r0_s1_g1a_route(token, out, (int)sizeof out, &out_len) != 0) return NULL;
    out[out_len] = 0;
    return out;
}

static const char *event(const char *token) {
    int out_len = 0;
    if (r0_s1_g1a_event(token, out, (int)sizeof out, &out_len) != 0) return NULL;
    out[out_len] = 0;
    return out;
}

static const char *event_value(const char *token, const char *value) {
    int out_len = 0;
    if (r0_s1_g1a_event_value(token, value, out, (int)sizeof out, &out_len) != 0) return NULL;
    out[out_len] = 0;
    return out;
}

static int has(const char *html, const char *needle) {
    return html != NULL && strstr(html, needle) != NULL;
}

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[131072];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, needle) != NULL;
}

int run_r0_s1_g1e_tests(void) {
    printf("g1e: bootstrap keeps readable quoted Glon source and no demo content\n");
    CHECK(file_contains("demo/shop/app.html", "\"Glon Demos\""),
          "A: bootstrap keeps readable quoted string literals");
    CHECK(!file_contains("demo/shop/app.html", "str-31: mk-string [84 101 97]") &&
          !file_contains("demo/shop/app.html", ": mk-string [8"),
          "B: view source has no str-N byte lowering");
    CHECK(file_contains("demo/shop/app.html", "style.css"),
          "C: view source references external style.css");
    CHECK(!file_contains("demo/shop/app.html", "A tiny GLON storefront") &&
          !file_contains("demo/shop/app.html", "Your basket") &&
          !file_contains("demo/shop/app.html", "The three paths") &&
          !file_contains("demo/shop/app.html", "How it works"),
          "D: bootstrap does NOT embed shop/guide/merchant-flow content");

    printf("g1e: launcher renders before any demo is loaded\n");

    r0_s1_init();
    CHECK(load_file("demo/shop/bootstrap.glon") == 0, "1: bootstrap loads");

    const char *r = route("home");
    CHECK(has(r, "Glon Demos") &&
          has(r, "data-glon-load='demos/shop.glon'") &&
          has(r, "data-glon-load='demos/guide.glon'") &&
          has(r, "data-glon-load='demos/merchant-flow.glon'"),
          "2: home renders the launcher with load-on-demand links");

    printf("g1e: selecting a demo loads and renders it\n");

    CHECK(load_file("demo/shop/demos/shop.glon") == 0, "3: shop.glon loads");
    r = route("shop");
    CHECK(has(r, "Glon Shop") && has(r, "data-glon-route='products'"),
          "4: home -> shop renders the shop landing after load");

    r = route("home");
    CHECK(has(r, "data-glon-route='shop'") &&
          !has(r, "data-glon-load='demos/shop.glon'"),
          "5: already-loaded demo is now a plain link (no reload)");

    CHECK(load_file("demo/shop/demos/guide.glon") == 0, "6: guide.glon loads");
    r = route("guide");
    CHECK(has(r, "Programmer's guide") && has(r, "demo/shop/README.md"),
          "7: home -> guide renders the guide after load");

    CHECK(load_file("demo/shop/demos/merchant-flow.glon") == 0, "8: merchant-flow.glon loads");
    r = route("merchant-flow");
    CHECK(has(r, "Julia merchant flow") && has(r, "merchant_flow.jl"),
          "9: home -> merchant-flow renders the Julia page + source link");

    printf("g1e: shop basket / events / persistence survive load-on-demand\n");

    r = route("products");
    CHECK(has(r, "0 items") && has(r, "data-glon-value='Tea'") && has(r, "data-glon-value='Rice'"),
          "10: products renders catalogue + empty basket");

    r = event_value("add-product", "Tea");
    CHECK(has(r, "Tea</span><span class='qty'>× 1</span>") && has(r, "1 item"),
          "11: Add Tea once => Tea x1");

    r = event_value("add-product", "Rice");
    CHECK(has(r, "Tea</span><span class='qty'>× 1</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "2 items"),
          "12: Add Rice once => Tea x1, Rice x1");

    r = event_value("add-product", "Tea");
    CHECK(has(r, "Tea</span><span class='qty'>× 2</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "3 items"),
          "13: Add Tea again => Tea x2, Rice x1, 3 items");

    route("home");
    r = route("products");
    CHECK(has(r, "Tea</span><span class='qty'>× 2</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "3 items"),
          "14: basket survives navigation away and back");

    r = event_value("search", "green tea");
    CHECK(has(r, "Search: green tea"), "15: search still works");

    printf("g1e: loaded STRING!/blocks/functions survive source-buffer reuse\n");
    /* `src` has been overwritten by guide.glon and merchant-flow.glon since
     * shop.glon was loaded; the shop's strings/blocks/closures must still be
     * intact (they were copied out of the source buffer at load time). */
    r = route("products");
    CHECK(has(r, "Glon Shop") || has(r, "Products"), "16: shop view intact after later loads");
    r = route("shop");
    CHECK(has(r, "Glon Shop"), "17: shop landing still renders after later loads");

    printf("g1e: failures and unknown routes are controlled\n");
    CHECK(has(route("definitely-unknown"), "Not found"), "18: unknown route still works");
    r = event("load-failed");
    CHECK(has(r, "Load failed"), "19: load-failed event renders a controlled error");

    return failures;
}
