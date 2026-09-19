/* r0_s1_g1b_tests.c - G1B minimal persistent-SPA dialect tests.
 *
 * G1B layers a small view dialect (heading / text / list / link) over the G1A
 * routing/render machinery. The dialect is ordinary GLON: a view is a block of
 * function calls, rendering is `do view-block`, and the result is a byte-list
 * block the unchanged r0_s1_g1a_render_fragment writes to the host.
 *
 * These tests build the dialect library + a test application as GLON source
 * (string literals already expanded to byte-lists, the same transformation
 * demo/shop/build.py performs), then route tokens exactly as the browser host
 * does and assert the rendered HTML. No S1 primitive, native id, or evaluator
 * change; the frozen substrate is untouched.
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
static size_t pn;

static void put(const char *s) { pn += (size_t)snprintf(prog_buf + pn, sizeof prog_buf - pn, "%s", s); }

/* append a byte-list literal "[ b0 b1 ... ]" for a C string */
static void bytes(const char *s) {
    put("[");
    for (const char *c = s; *c; c++) {
        pn += (size_t)snprintf(prog_buf + pn, sizeof prog_buf - pn, " %d", (int)(unsigned char)*c);
    }
    put(" ]");
}

/* build the G1B library + a test application */
static char *build_program(void) {
    pn = 0;
    put("[");
    /* byte output + loader-block primitives (RAW trapdoor) */
    put(" emit-clear: raw [ LIT 0 LIT SCRATCH_E ! ARITY 0 EXIT ] ");
    put(" emit-byte: raw 1 [ LIT SCRATCH_E @ LIT G1_OUT_DATA ADD ! LIT SCRATCH_E @ LIT 1 ADD LIT SCRATCH_E ! ARITY 0 EXIT ] ");
    put(" emit-finish: raw [ LIT SCRATCH_E @ LIT G1_OUT ! LIT G1_OUT LIT T_BLOCK ADD ARITY 1 EXIT ] ");
    put(" block-len: raw 1 [ DUP LIT 16 MOD SUB @ LIT 16 MUL ARITY 1 EXIT ] ");
    put(" block-at: raw 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD @ ARITY 1 EXIT ] ");
    /* text emission */
    put(" emit-int: func [n] [ either < n 0 [ emit-byte 45 emit-digits - 0 n ] [ emit-digits n ] ] ");
    put(" emit-digits: func [n] [ either > n 9 [ emit-digits / n 10 ] [ none ] emit-byte + 48 - n * 10 / n 10 ] ");
    put(" emit-text: func [blk] [ emit-text-at blk 0 block-len blk ] ");
    put(" emit-text-at: func [blk i n] [ either < i n [ emit-byte block-at blk i emit-text-at blk + i 1 n ] [ none ] ] ");
    /* HTML tag atoms */
    put(" h1-open: "); bytes("<h1>");
    put(" h1-close: "); bytes("</h1>");
    put(" p-open: "); bytes("<p>");
    put(" p-close: "); bytes("</p>");
    put(" ul-open: "); bytes("<ul>");
    put(" ul-close: "); bytes("</ul>");
    put(" li-open: "); bytes("<li>");
    put(" li-close: "); bytes("</li>");
    put(" btn-open: "); bytes("<button data-glon-route='");
    put(" btn-mid: "); bytes("'>");
    put(" btn-close: "); bytes("</button>");
    /* view vocabulary */
    put(" heading: func [content] [ emit-text h1-open do content emit-text h1-close ] ");
    put(" text: func [content] [ emit-text p-open do content emit-text p-close ] ");
    put(" link: func [content route] [ emit-text btn-open emit-text route emit-text btn-mid do content emit-text btn-close ] ");
    put(" list: func [items] [ emit-text ul-open each-product items emit-text ul-close ] ");
    put(" each-product: func [items] [ emit-product-at items 0 block-len items ] ");
    put(" emit-product-at: func [items i n] [ either < i n [ emit-text li-open emit-text block-at items i emit-text li-close emit-product-at items + i 1 n ] [ none ] ] ");
    /* application */
    put(" visit-count: 0 ");
    put(" catalog: ["); bytes("Tea"); bytes("Rice"); put(" ] ");
    put(" home-view: [ heading [ emit-text "); bytes("Glon Shop");
    put(" ] link [ emit-text "); bytes("View products"); put(" ] "); bytes("products"); put(" ] ");
    put(" products-view: [ heading [ emit-text "); bytes("Products");
    put(" ] list catalog text [ emit-text "); bytes("Products page visits: ");
    put(" emit-int visit-count ] link [ emit-text "); bytes("Back home"); put(" ] "); bytes("home"); put(" ] ");
    put(" not-found-view: [ heading [ emit-text "); bytes("Not found");
    put(" ] link [ emit-text "); bytes("Back home"); put(" ] "); bytes("home"); put(" ] ");
    put(" go-home: func [] [ emit-clear do home-view emit-finish ] ");
    put(" go-products: func [] [ visit-count: + visit-count 1 emit-clear do products-view emit-finish ] ");
    put(" go-not-found: func [] [ emit-clear do not-found-view emit-finish ] ");
    put(" route: [ either = current-route 'products [ go-products ] [ either = current-route 'home [ go-home ] [ go-not-found ] ] ] ");
    put("]");
    return prog_buf;
}

/* fresh load + route a token; returns rendered length or -1 */
static int route(const char *token) {
    r0_s1_init();
    int err = 0;
    cell block = r0_s1_parse(build_program(), &err);
    if (err) return -1;
    r0_s1_run(block);
    int out_len = 0;
    if (r0_s1_g1a_route(token, out_buf, (int)sizeof out_buf, &out_len) != 0) return -1;
    out_buf[out_len] = 0;
    return out_len;
}

static void test_home(void) {
    printf("g1b: 1 home view renders heading + link\n");
    int n = route("home");
    const char *want = "<h1>Glon Shop</h1><button data-glon-route='products'>View products</button>";
    CHECK(n > 0 && strcmp(out_buf, want) == 0, "1: home view renders");
    if (n > 0 && strcmp(out_buf, want)) printf("     got: %s\n", out_buf);
}

static void test_products_composition(void) {
    printf("g1b: 2 products view composes heading + list + text + link\n");
    int n = route("products");
    const char *want =
        "<h1>Products</h1>"
        "<ul><li>Tea</li><li>Rice</li></ul>"
        "<p>Products page visits: 1</p>"
        "<button data-glon-route='home'>Back home</button>";
    CHECK(n > 0 && strcmp(out_buf, want) == 0, "2: products view composes (visit 1)");
    if (n > 0 && strcmp(out_buf, want)) printf("     got: %s\n", out_buf);
}

static void test_persistent_state(void) {
    printf("g1b: 3 persistent state survives route changes\n");
    r0_s1_init();
    int err = 0;
    cell block = r0_s1_parse(build_program(), &err);
    if (err) { CHECK(0, "3: program loads"); return; }
    r0_s1_run(block);
    int ok = 1;
    for (int i = 1; i <= 3 && ok; i++) {
        int out_len = 0;
        if (r0_s1_g1a_route("products", out_buf, (int)sizeof out_buf, &out_len) != 0) { ok = 0; break; }
        out_buf[out_len] = 0;
        char want[256];
        snprintf(want, sizeof want,
            "<h1>Products</h1><ul><li>Tea</li><li>Rice</li></ul>"
            "<p>Products page visits: %d</p>"
            "<button data-glon-route='home'>Back home</button>", i);
        if (strcmp(out_buf, want) != 0) ok = 0;
    }
    CHECK(ok, "3: visit-count increments 1,2,3 across three entries");
}

static void test_unknown_route(void) {
    printf("g1b: 4 unknown route renders not-found\n");
    int n = route("nope");
    const char *want = "<h1>Not found</h1><button data-glon-route='home'>Back home</button>";
    CHECK(n > 0 && strcmp(out_buf, want) == 0, "4: unknown route -> not-found view");
    if (n > 0 && strcmp(out_buf, want)) printf("     got: %s\n", out_buf);
}

int run_r0_s1_g1b_tests(void) {
    test_home();
    test_products_composition();
    test_persistent_state();
    test_unknown_route();
    return failures;
}
