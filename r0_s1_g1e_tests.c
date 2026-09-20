/* r0_s1_g1e_tests.c - G1E repeated interactive composition tests.
 *
 * G1E proves that ordinary Glon composition can generate a repeated family of
 * interactive controls, each carrying its own application data, while the
 * generic host bridge stays application-agnostic.
 *
 * These tests load the ACTUAL generated artefact (demo/shop/bundle.glon,
 * produced by demo/shop/build.py) and drive routes and value-bearing events
 * exactly as the browser host does. They assert the *rendered* basket HTML
 * (per-product quantities), not internal counters. The source of truth is the
 * bundle; this fixture does not duplicate the application.
 */

#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char prog[65536];
static char out[8192];

static const char *route(const char *token) {
    int out_len = 0;
    if (r0_s1_g1a_route(token, out, (int)sizeof out, &out_len) != 0) return NULL;
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
    printf("g1e: generated page shows readable quoted Glon source\n");
    CHECK(file_contains("demo/shop/app.html", "\"Glon Shop\"") &&
          file_contains("demo/shop/app.html", "\"Tea\"") &&
          file_contains("demo/shop/app.html", "\"Rice\""),
          "A: view source keeps quoted string literals");
    CHECK(!file_contains("demo/shop/app.html", "str-31: mk-string [84 101 97]") &&
          !file_contains("demo/shop/app.html", ": mk-string [8"),
          "B: view source has no str-N byte lowering");
    CHECK(file_contains("demo/shop/app.html", "style.css"),
          "C: view source references external style.css");

    printf("g1e: shop bundle renders the basket with per-product quantities\n");

    FILE *f = fopen("demo/shop/bundle.glon", "rb");
    if (!f) { printf("  FAIL: cannot open demo/shop/bundle.glon (run demo/shop/build.py first)\n"); failures++; return failures; }
    size_t n = fread(prog, 1, sizeof prog - 1, f);
    fclose(f);
    prog[n] = 0;

    r0_s1_init();
    int err = 0;
    cell block = r0_s1_parse(prog, &err);
    if (err) { printf("  FAIL: bundle parse error\n"); failures++; return failures; }
    r0_s1_run(block);

    const char *r;

    CHECK(has(route("home"), "Glon Shop"), "1: home renders the brand heading");

    r = route("products");
    CHECK(has(r, "0 items") && has(r, "data-glon-value='Tea'") && has(r, "data-glon-value='Rice'"),
          "2: products renders catalogue + empty basket");

    r = event_value("add-product", "Tea");
    CHECK(has(r, "Tea</span><span class='qty'>× 1</span>") && has(r, "1 item"),
          "3: Add Tea once => Tea x1");

    r = event_value("add-product", "Rice");
    CHECK(has(r, "Tea</span><span class='qty'>× 1</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "2 items"),
          "4: Add Rice once => Tea x1, Rice x1");

    r = event_value("add-product", "Tea");
    CHECK(has(r, "Tea</span><span class='qty'>× 2</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "3 items"),
          "5: Add Tea again => Tea x2, Rice x1, 3 items");

    route("home");
    r = route("products");
    CHECK(has(r, "Tea</span><span class='qty'>× 2</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "3 items"),
          "6: basket survives navigation away and back");

    r = event_value("search", "green tea");
    CHECK(has(r, "Search: green tea"), "7: search still works");

    CHECK(has(route("definitely-unknown"), "Not found"), "8: unknown route still works");

    return failures;
}
