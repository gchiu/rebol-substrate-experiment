/* r0_s1_g1c_tests.c - G1C generic SPA application events tests.
 *
 * G1C proves a generic browser->Glon application event path on top of G1B:
 *
 *     click button -> event token -> persistent Glon machine
 *                   -> mutate state -> rerender current view
 *
 * The host (r0_s1_g1a_event / glon_event) forwards a bare token; the meaning
 * of the token lives entirely in GLON (`do-event`). These tests load the
 * application once, then drive routes and events exactly as the browser host
 * does, asserting state changes and that state persists across navigation.
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
static void bytes(const char *s) {
    put("[");
    for (const char *c = s; *c; c++)
        pn += (size_t)snprintf(prog_buf + pn, sizeof prog_buf - pn, " %d", (int)(unsigned char)*c);
    put(" ]");
}

static char *build_program(void) {
    pn = 0;
    put("[");
    /* byte output + block primitives (RAW trapdoor) */
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
    put(" h1-open: "); bytes("<h1>"); put(" h1-close: "); bytes("</h1>");
    put(" p-open: "); bytes("<p>"); put(" p-close: "); bytes("</p>");
    put(" ul-open: "); bytes("<ul>"); put(" ul-close: "); bytes("</ul>");
    put(" li-open: "); bytes("<li>"); put(" li-close: "); bytes("</li>");
    put(" link-open: "); bytes("<button data-glon-route='");
    put(" btn-mid: "); bytes("'>"); put(" btn-close: "); bytes("</button>");
    put(" event-open: "); bytes("<button data-glon-event='");
    /* view vocabulary */
    put(" heading: func [content] [ emit-text h1-open do content emit-text h1-close ] ");
    put(" text: func [content] [ emit-text p-open do content emit-text p-close ] ");
    put(" link: func [content route] [ emit-text link-open emit-text route emit-text btn-mid do content emit-text btn-close ] ");
    put(" button: func [content event] [ emit-text event-open emit-text event emit-text btn-mid do content emit-text btn-close ] ");
    put(" list: func [items] [ emit-text ul-open each-product items emit-text ul-close ] ");
    put(" each-product: func [items] [ emit-product-at items 0 block-len items ] ");
    put(" emit-product-at: func [items i n] [ either < i n [ emit-text li-open emit-text block-at items i emit-text li-close emit-product-at items + i 1 n ] [ none ] ] ");
    /* application */
    put(" visit-count: 0 cart-count: 0 ");
    put(" catalog: ["); bytes("Tea"); bytes("Rice"); put(" ] ");
    put(" home-view: [ heading [ emit-text "); bytes("Glon Shop");
    put(" ] link [ emit-text "); bytes("View products"); put(" ] "); bytes("products"); put(" ] ");
    put(" products-view: [ heading [ emit-text "); bytes("Products");
    put(" ] list catalog text [ emit-text "); bytes("Products page visits: ");
    put(" emit-int visit-count ] text [ emit-text "); bytes("Cart: ");
    put(" emit-int cart-count ] button [ emit-text "); bytes("Add"); put(" ] "); bytes("add-one");
    put(" link [ emit-text "); bytes("Back home"); put(" ] "); bytes("home"); put(" ] ");
    put(" not-found-view: [ heading [ emit-text "); bytes("Not found");
    put(" ] link [ emit-text "); bytes("Back home"); put(" ] "); bytes("home"); put(" ] ");
    put(" render-home: func [] [ emit-clear do home-view emit-finish ] ");
    put(" render-products: func [] [ emit-clear do products-view emit-finish ] ");
    put(" render-not-found: func [] [ emit-clear do not-found-view emit-finish ] ");
    put(" render-current: func [] [ either = current-route 'products [ render-products ] [ either = current-route 'home [ render-home ] [ render-not-found ] ] ] ");
    put(" go-home: func [] [ render-current ] ");
    put(" go-products: func [] [ visit-count: + visit-count 1 render-current ] ");
    put(" go-not-found: func [] [ render-current ] ");
    put(" route: [ either = current-route 'products [ go-products ] [ either = current-route 'home [ go-home ] [ go-not-found ] ] ] ");
    put(" add-one: func [] [ cart-count: + cart-count 1 render-current ] ");
    put(" do-event: [ either = current-event 'add-one [ add-one ] [ render-current ] ] ");
    put("]");
    return prog_buf;
}

/* load once, then reuse the persistent machine for every route/event */
static const char *route(const char *token) {
    int out_len = 0;
    if (r0_s1_g1a_route(token, out_buf, (int)sizeof out_buf, &out_len) != 0) return NULL;
    out_buf[out_len] = 0;
    return out_buf;
}

static const char *event(const char *token) {
    int out_len = 0;
    if (r0_s1_g1a_event(token, out_buf, (int)sizeof out_buf, &out_len) != 0) return NULL;
    out_buf[out_len] = 0;
    return out_buf;
}

static const char *products_html(int visits, int cart) {
    static char buf[512];
    snprintf(buf, sizeof buf,
        "<h1>Products</h1><ul><li>Tea</li><li>Rice</li></ul>"
        "<p>Products page visits: %d</p><p>Cart: %d</p>"
        "<button data-glon-event='add-one'>Add</button>"
        "<button data-glon-route='home'>Back home</button>", visits, cart);
    return buf;
}

int run_r0_s1_g1c_tests(void) {
    printf("g1c: 1 load once, navigate to products, initial cart 0\n");
    r0_s1_init();
    int err = 0;
    cell block = r0_s1_parse(build_program(), &err);
    r0_s1_run(block);
    const char *r = route("products");
    CHECK(r && strcmp(r, products_html(1, 0)) == 0, "1: products -> visits 1, cart 0");
    if (r && strcmp(r, products_html(1, 0))) printf("     got: %s\n", r);

    printf("g1c: 2 trigger add-one event\n");
    r = event("add-one");
    CHECK(r && strcmp(r, products_html(1, 1)) == 0, "2: add-one -> cart 1 (visits unchanged)");
    if (r && strcmp(r, products_html(1, 1))) printf("     got: %s\n", r);

    printf("g1c: 3 trigger add-one again\n");
    r = event("add-one");
    CHECK(r && strcmp(r, products_html(1, 2)) == 0, "3: add-one -> cart 2");

    printf("g1c: 4 navigate home then back; cart persists\n");
    route("home");
    r = route("products");
    CHECK(r && strcmp(r, products_html(2, 2)) == 0, "4: products again -> visits 2, cart 2 persists");

    printf("g1c: 5 unknown event falls back to current view\n");
    r = event("bogus-event");
    CHECK(r && strcmp(r, products_html(2, 2)) == 0, "5: unknown event rerenders current view (no change)");

    return failures;
}
