/* r0_s1_g1e_tests.c - G1E repeated interactive composition tests.
 *
 * G1E proves that ordinary Glon composition can generate a repeated family of
 * interactive controls, each carrying its own application data, while the
 * generic host bridge stays application-agnostic:
 *
 *     repeated "Add" buttons -> same event token "add-product"
 *                             -> different values "tea" / "rice"
 *                             -> Glon mutates persistent state per value
 *
 * No new host API is needed: the G1D `event(token, value)` bridge carries the
 * product identity as the value (a byte-list). These tests load the app once,
 * then drive routes and value-bearing events exactly as the host does.
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
    put(" emit-clear: raw [ LIT 0 LIT SCRATCH_E ! ARITY 0 EXIT ] ");
    put(" emit-byte: raw 1 [ LIT SCRATCH_E @ LIT G1_OUT_DATA ADD ! LIT SCRATCH_E @ LIT 1 ADD LIT SCRATCH_E ! ARITY 0 EXIT ] ");
    put(" emit-finish: raw [ LIT SCRATCH_E @ LIT G1_OUT ! LIT G1_OUT LIT T_BLOCK ADD ARITY 1 EXIT ] ");
    put(" block-len: raw 1 [ DUP LIT 16 MOD SUB @ LIT 16 MUL ARITY 1 EXIT ] ");
    put(" block-at: raw 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD @ ARITY 1 EXIT ] ");
    put(" emit-int: func [n] [ either < n 0 [ emit-byte 45 emit-digits - 0 n ] [ emit-digits n ] ] ");
    put(" emit-digits: func [n] [ either > n 9 [ emit-digits / n 10 ] [ none ] emit-byte + 48 - n * 10 / n 10 ] ");
    put(" emit-text: func [blk] [ emit-text-at blk 0 block-len blk ] ");
    put(" emit-text-at: func [blk i n] [ either < i n [ emit-byte block-at blk i emit-text-at blk + i 1 n ] [ none ] ] ");
    put(" h1-open: "); bytes("<h1>"); put(" h1-close: "); bytes("</h1>");
    put(" p-open: "); bytes("<p>"); put(" p-close: "); bytes("</p>");
    put(" ul-open: "); bytes("<ul>"); put(" ul-close: "); bytes("</ul>");
    put(" li-open: "); bytes("<li>"); put(" li-close: "); bytes("</li>");
    put(" link-open: "); bytes("<button data-glon-route='");
    put(" btn-mid: "); bytes("'>"); put(" btn-close: "); bytes("</button>");
    put(" event-open: "); bytes("<button data-glon-event='");
    put(" input-open: "); bytes("<input data-glon-input='");
    put(" value-mid: "); bytes("' data-glon-value='");
    put(" heading: func [content] [ emit-text h1-open do content emit-text h1-close ] ");
    put(" text: func [content] [ emit-text p-open do content emit-text p-close ] ");
    put(" link: func [content route] [ emit-text link-open emit-text route emit-text btn-mid do content emit-text btn-close ] ");
    put(" button: func [content event] [ emit-text event-open emit-text event emit-text btn-mid do content emit-text btn-close ] ");
    put(" value-button: func [content event value] [ emit-text event-open emit-text event emit-text value-mid emit-text value emit-text btn-mid do content emit-text btn-close ] ");
    put(" input: func [name] [ emit-text input-open emit-text name emit-text btn-mid ] ");
    put(" products: func [items] [ emit-text ul-open each-product items emit-text ul-close ] ");
    put(" each-product: func [items] [ emit-product-at items 0 block-len items ] ");
    put(" emit-product-at: func [items i n] [ either < i n [ emit-text li-open emit-text block-at items i emit-byte 32 value-button [ emit-text "); bytes("Add");
    put(" ] "); bytes("add-product"); put(" block-at items i emit-text li-close emit-product-at items + i 1 n ] [ none ] ] ");
    /* application */
    put(" visit-count: 0 cart-count: 0 last-added: [] search-term: [] ");
    put(" catalog: ["); bytes("Tea"); bytes("Rice"); put(" ] ");
    put(" home-view: [ heading [ emit-text "); bytes("Glon Shop");
    put(" ] link [ emit-text "); bytes("View products"); put(" ] "); bytes("products"); put(" ] ");
    put(" products-view: [ heading [ emit-text "); bytes("Products");
    put(" ] products catalog text [ emit-text "); bytes("Products page visits: ");
    put(" emit-int visit-count ] text [ emit-text "); bytes("Search: ");
    put(" emit-text search-term ] input "); bytes("search");
    put(" button [ emit-text "); bytes("Search"); put(" ] "); bytes("search");
    put(" text [ emit-text "); bytes("Cart: ");
    put(" emit-int cart-count ] text [ emit-text "); bytes("Last added: ");
    put(" emit-text last-added ] link [ emit-text "); bytes("Back home"); put(" ] "); bytes("home"); put(" ] ");
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
    put(" add-product: func [] [ cart-count: + cart-count 1 last-added: current-value render-current ] ");
    put(" search: func [] [ search-term: current-value render-current ] ");
    put(" do-event: [ either = current-event 'add-product [ add-product ] [ either = current-event 'search [ search ] [ render-current ] ] ] ");
    put("]");
    return prog_buf;
}

static const char *route(const char *token) {
    int out_len = 0;
    if (r0_s1_g1a_route(token, out_buf, (int)sizeof out_buf, &out_len) != 0) return NULL;
    out_buf[out_len] = 0;
    return out_buf;
}

static const char *event_value(const char *token, const char *value) {
    int out_len = 0;
    if (r0_s1_g1a_event_value(token, value, out_buf, (int)sizeof out_buf, &out_len) != 0) return NULL;
    out_buf[out_len] = 0;
    return out_buf;
}

static const char *products_html(int visits, int cart, const char *last, const char *term) {
    static char buf[2048];
    snprintf(buf, sizeof buf,
        "<h1>Products</h1>"
        "<ul>"
        "<li>Tea <button data-glon-event='add-product' data-glon-value='Tea'>Add</button></li>"
        "<li>Rice <button data-glon-event='add-product' data-glon-value='Rice'>Add</button></li>"
        "</ul>"
        "<p>Products page visits: %d</p><p>Search: %s</p>"
        "<input data-glon-input='search'><button data-glon-event='search'>Search</button>"
        "<p>Cart: %d</p><p>Last added: %s</p>"
        "<button data-glon-route='home'>Back home</button>", visits, term, cart, last);
    return buf;
}

int run_r0_s1_g1e_tests(void) {
    printf("g1e: 1 load once, products renders a repeated interactive list\n");
    r0_s1_init();
    int err = 0;
    cell block = r0_s1_parse(build_program(), &err);
    r0_s1_run(block);
    const char *r = route("products");
    CHECK(r && strcmp(r, products_html(1, 0, "", "")) == 0, "1: two products, same event token, distinct values");
    if (r && strcmp(r, products_html(1, 0, "", ""))) printf("     got: %s\n", r);

    printf("g1e: 2 add-product for Tea\n");
    r = event_value("add-product", "Tea");
    CHECK(r && strcmp(r, products_html(1, 1, "Tea", "")) == 0, "2: tea -> cart 1, last-added Tea");
    if (r && strcmp(r, products_html(1, 1, "Tea", ""))) printf("     got: %s\n", r);

    printf("g1e: 3 add-product for Rice\n");
    r = event_value("add-product", "Rice");
    CHECK(r && strcmp(r, products_html(1, 2, "Rice", "")) == 0, "3: rice -> cart 2, last-added Rice");

    printf("g1e: 4 add-product for Tea again (identity not conflated)\n");
    r = event_value("add-product", "Tea");
    CHECK(r && strcmp(r, products_html(1, 3, "Tea", "")) == 0, "4: tea again -> cart 3, last-added Tea");

    printf("g1e: 5 state persists across route changes\n");
    route("home");
    r = route("products");
    CHECK(r && strcmp(r, products_html(2, 3, "Tea", "")) == 0, "5: home -> products -> cart 3, last-added Tea persists");

    printf("g1e: 6 unknown event with value falls back safely\n");
    r = event_value("bogus", "ignored");
    CHECK(r && strcmp(r, products_html(2, 3, "Tea", "")) == 0, "6: unknown event -> current view (no change)");

    return failures;
}
