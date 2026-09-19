/* r0_s1_g1a_tests.c - G1A template engine + routing primitive tests.
 *
 * These test the browser-agnostic layer (r0_s1_g1a.c) headlessly: fragment
 * selection by route, embedded-Glon detection/evaluation, persistent state
 * across route changes, unknown-route behaviour, malformed embedded-block
 * behaviour, and verbatim rendering of plain HTML. The frozen S1 substrate and
 * the R0 evaluator/parser are untouched.
 *
 * A fragment here is a loader-heap BLOCK of integer byte codes (the same
 * representation demo/shop/build.py generates from the demo/shop/fragments
 * HTML files), because GLON has no string literals and managed STRING!
 * objects do not persist across r0_s1_run calls.
 */

#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char prog_buf[65536];
static char out_buf[8192];

/* append a byte-list definition "name-fragment: [ b0 b1 ... ]" for `s` */
static void emit_bytes(char *dst, size_t cap, size_t *n, const char *name, const char *s) {
    int used = snprintf(dst + *n, cap - *n, " %s-fragment: [", name);
    *n += (size_t)used;
    for (const char *p = s; *p; p++) {
        used = snprintf(dst + *n, cap - *n, " %d", (int)(unsigned char)*p);
        *n += (size_t)used;
    }
    used = snprintf(dst + *n, cap - *n, " ]");
    *n += (size_t)used;
}

/* build a complete G1A program: fragments + state + router */
static char *build_program(const char *home, const char *products, const char *notfound) {
    size_t n = 0;
    n += (size_t)snprintf(prog_buf + n, sizeof prog_buf - n, "[");
    emit_bytes(prog_buf, sizeof prog_buf, &n, "home", home);
    emit_bytes(prog_buf, sizeof prog_buf, &n, "products", products);
    emit_bytes(prog_buf, sizeof prog_buf, &n, "not-found", notfound);
    n += (size_t)snprintf(prog_buf + n, sizeof prog_buf - n,
        " visit-count: 0 "
        " current-route: none "
        " home-page: [ home-fragment ] "
        " products-page: [ products-fragment ] "
        " not-found-page: [ not-found-fragment ] "
        " route: [ either = current-route 'products [ do products-page ] "
        "          [ either = current-route 'home [ do home-page ] [ do not-found-page ] ] ] "
        "]");
    return prog_buf;
}

/* fresh load of the given program, then route a token; returns out_len or -1 */
static int route(const char *program, const char *token) {
    r0_s1_init();
    int err = 0;
    cell block = r0_s1_parse(program, &err);
    if (err) return -1;
    r0_s1_run(block);
    int out_len = 0;
    int rc = r0_s1_g1a_route(token, out_buf, (int)sizeof out_buf, &out_len);
    if (rc != 0) return -1;
    out_buf[out_len] = 0;
    return out_len;
}

static void test_plain_html(void) {
    printf("g1a: 1 ordinary HTML renders unchanged\n");
    int n = route(build_program("<h1>Glon Shop</h1>\n", "p", "nf"), "home");
    CHECK(n > 0 && strcmp(out_buf, "<h1>Glon Shop</h1>\n") == 0,
          "1: fragment with no <% %> renders verbatim");
}

static void test_embedded_eval(void) {
    printf("g1a: 2 embedded Glon detected and evaluated\n");
    int n = route(build_program("h", "sum=<% + 2 3 %>", "nf"), "products");
    CHECK(n > 0 && strcmp(out_buf, "sum=5") == 0,
          "2: <% + 2 3 %> renders \"sum=5\"");
}

static void test_fragment_selection(void) {
    printf("g1a: 3 fragment selected by route\n");
    const char *prog = build_program("HOME", "PRODUCTS", "NOTFOUND");
    int a = route(prog, "home");
    char home_copy[128]; strncpy(home_copy, out_buf, sizeof home_copy - 1); home_copy[sizeof home_copy - 1] = 0;
    int b = route(prog, "products");
    char products_copy[128]; strncpy(products_copy, out_buf, sizeof products_copy - 1); products_copy[sizeof products_copy - 1] = 0;
    int c = route(prog, "foo");
    CHECK(a > 0 && strcmp(home_copy, "HOME") == 0, "3: route home -> HOME fragment");
    CHECK(b > 0 && strcmp(products_copy, "PRODUCTS") == 0, "3: route products -> PRODUCTS fragment");
    CHECK(c > 0 && strcmp(out_buf, "NOTFOUND") == 0, "3: unknown route -> NOTFOUND fragment");
}

static void test_unknown_route(void) {
    printf("g1a: 4 unknown route behaviour\n");
    int n = route(build_program("h", "p", "PAGE NOT FOUND"), "does-not-exist");
    CHECK(n > 0 && strcmp(out_buf, "PAGE NOT FOUND") == 0,
          "4: unknown token renders the not-found fragment");
}

static void test_malformed_block(void) {
    printf("g1a: 5 malformed embedded block is handled without crashing\n");
    /* no closing %> -> everything from <% is literal */
    int n1 = route(build_program("h", "a<% [unclosed", "nf"), "products");
    CHECK(n1 > 0 && strcmp(out_buf, "a<% [unclosed") == 0,
          "5: unclosed <% renders as literal text");

    /* parse error in the embedded block -> block skipped */
    int n2 = route(build_program("h", "x<% + ] %>y", "nf"), "products");
    CHECK(n2 > 0 && strcmp(out_buf, "xy") == 0,
          "5: unparsable embedded block is skipped (literal text kept)");
}

static void test_persistent_state(void) {
    printf("g1a: 6 persistent state survives multiple route changes\n");
    const char *prog = build_program("h", "visits=<% visit-count: + visit-count 1 none %><% visit-count %>", "nf");
    r0_s1_init();
    int err = 0;
    cell block = r0_s1_parse(prog, &err);
    CHECK(!err, "6: program loads");
    r0_s1_run(block);

    int ok = 1;
    for (int i = 1; i <= 3 && ok; i++) {
        int out_len = 0;
        if (r0_s1_g1a_route("products", out_buf, (int)sizeof out_buf, &out_len) != 0) { ok = 0; break; }
        out_buf[out_len] = 0;
        char want[32];
        snprintf(want, sizeof want, "visits=%d", i);
        if (strcmp(out_buf, want) != 0) ok = 0;
    }
    CHECK(ok, "6: visit-count increments 1,2,3 across three route changes");
}

int run_r0_s1_g1a_tests(void) {
    test_plain_html();
    test_embedded_eval();
    test_fragment_selection();
    test_unknown_route();
    test_malformed_block();
    test_persistent_state();
    return failures;
}
