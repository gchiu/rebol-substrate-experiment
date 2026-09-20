/* r0_s1_g1d_tests.c - G1D value-bearing SPA event tests.
 *
 * G1D extends the generic event path (G1C) so a browser interaction can pass
 * an opaque token AND an ordinary value into the persistent Glon machine, while
 * JavaScript remains ignorant of the meaning of either:
 *
 *     DOM interaction -> event token + value -> Glon -> mutate state -> rerender
 *
 * The value is represented as a loader-heap byte-list block (GLON has no string
 * literals), bound to `current-value` before `do-event` runs. These tests load
 * the application once, then drive routes and value-bearing events exactly as
 * the host does.
 */

#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char prog_buf[16384];   /* atoms + view vocabulary + application */
static char prim_buf[4096];    /* RAW primitives (including mk-string) */
static char defs_buf[8192];    /* hoisted str-N: mk-string [ ... ] definitions */
static char out_buf[8192];
static size_t pn, prim_pn, defs_pn;

static void put(const char *s)  { pn      += (size_t)snprintf(prog_buf + pn, sizeof prog_buf - pn, "%s", s); }
static void pput(const char *s) { prim_pn += (size_t)snprintf(prim_buf + prim_pn, sizeof prim_buf - prim_pn, "%s", s); }
static void dput(const char *s) { defs_pn += (size_t)snprintf(defs_buf + defs_pn, sizeof defs_buf - defs_pn, "%s", s); }

/* Hoist a string literal to a top-level `str-N: mk-string [ b0 b1 ... ]` so the
 * string is built ONCE at load (not per-render, which would clobber the shared
 * SCRATCH_E output length mid-render). Returns the `str-N` reference. */
#define MAX_HOIST 64
static const char *hoist_strs[MAX_HOIST];
static int n_hoist;
static const char *hoist(const char *s) {
    static char ref[16];
    for (int i = 0; i < n_hoist; i++) {
        if (strcmp(hoist_strs[i], s) == 0) {
            snprintf(ref, sizeof ref, "str-%d", i + 1);
            return ref;
        }
    }
    int idx = n_hoist++;
    hoist_strs[idx] = s;
    snprintf(ref, sizeof ref, "str-%d", idx + 1);
    dput(" "); dput(ref); dput(": mk-string [");
    for (const char *c = s; *c; c++) {
        char num[8];
        snprintf(num, sizeof num, " %d", (int)(unsigned char)*c);
        dput(num);
    }
    dput(" ]");
    return ref;
}

static char *build_program(void) {
    pn = prim_pn = defs_pn = 0;
    n_hoist = 0;

    pput(" emit-clear: raw [ LIT 0 LIT SCRATCH_E ! ARITY 0 EXIT ] ");
    pput(" emit-byte: raw 1 [ LIT SCRATCH_E @ LIT G1_OUT_DATA ADD ! LIT SCRATCH_E @ LIT 1 ADD LIT SCRATCH_E ! ARITY 0 EXIT ] ");
    pput(" emit-finish: raw [ LIT SCRATCH_E @ LIT G1_OUT ! LIT G1_OUT LIT T_BLOCK ADD ARITY 1 EXIT ] ");
    pput(" block-len: raw 1 [ DUP LIT 16 MOD SUB @ LIT 16 MUL ARITY 1 EXIT ] ");
    pput(" block-at: raw 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD @ ARITY 1 EXIT ] ");
    pput(" mk-string: raw 1 [ DUP LIT 16 MOD SUB LIT SCRATCH_B ! LIT SCRATCH_B @ @ LIT SCRATCH_F ! LIT SCRATCH_F @ LIT 1 ADD LIT 15 ADD LIT 16 DIV LIT 16 MUL LIT GC_KIND_STRING CALL alloc LIT SCRATCH_E ! LIT SCRATCH_F @ LIT SCRATCH_E @ ! LIT 0 LIT SCRATCH_A ! mk-loop: LIT SCRATCH_A @ LIT SCRATCH_F @ LT ZBRANCH mk-done LIT SCRATCH_B @ LIT 2 ADD LIT SCRATCH_A @ ADD @ LIT 16 DIV LIT SCRATCH_E @ LIT 1 ADD LIT SCRATCH_A @ ADD ! LIT SCRATCH_A @ LIT 1 ADD LIT SCRATCH_A ! BRANCH mk-loop mk-done: LIT SCRATCH_E @ LIT T_STRING ADD ARITY 1 EXIT ] ");
    pput(" get: raw 1 [ CALL lookup ARITY 1 EXIT ] ");
    pput(" emit-int: func [n] [ either < n 0 [ emit-byte 45 emit-digits - 0 n ] [ emit-digits n ] ] ");
    pput(" emit-digits: func [n] [ either > n 9 [ emit-digits / n 10 ] [ none ] emit-byte + 48 - n * 10 / n 10 ] ");
    pput(" emit-text: raw 1 [ DUP LIT 16 MOD SUB LIT SCRATCH_B ! LIT 0 LIT SCRATCH_A ! LIT SCRATCH_B @ @ LIT SCRATCH_F ! emit-text-loop: LIT SCRATCH_A @ LIT SCRATCH_F @ LT ZBRANCH emit-text-done LIT SCRATCH_B @ LIT 1 ADD LIT SCRATCH_A @ ADD @ LIT 16 MUL LIT G1_OUT_DATA LIT SCRATCH_E @ ADD ! LIT SCRATCH_E @ LIT 1 ADD LIT SCRATCH_E ! LIT SCRATCH_A @ LIT 1 ADD LIT SCRATCH_A ! BRANCH emit-text-loop emit-text-done: ARITY 0 EXIT ] ");

    put(" h1-open: "); put(hoist("<h1>")); put(" h1-close: "); put(hoist("</h1>"));
    put(" p-open: "); put(hoist("<p>")); put(" p-close: "); put(hoist("</p>"));
    put(" ul-open: "); put(hoist("<ul>")); put(" ul-close: "); put(hoist("</ul>"));
    put(" li-open: "); put(hoist("<li>")); put(" li-close: "); put(hoist("</li>"));
    put(" link-open: "); put(hoist("<button data-glon-route='"));
    put(" btn-mid: "); put(hoist("'>")); put(" btn-close: "); put(hoist("</button>"));
    put(" event-open: "); put(hoist("<button data-glon-event='"));
    put(" input-open: "); put(hoist("<input data-glon-input='"));
    put(" heading: func [content] [ emit-text h1-open do content emit-text h1-close ] ");
    put(" text: func [content] [ emit-text p-open do content emit-text p-close ] ");
    put(" link: func [content route] [ emit-text link-open emit-text route emit-text btn-mid do content emit-text btn-close ] ");
    put(" button: func [content event] [ emit-text event-open emit-text event emit-text btn-mid do content emit-text btn-close ] ");
    put(" input: func [name] [ emit-text input-open emit-text name emit-text btn-mid ] ");
    put(" list: func [items] [ emit-text ul-open each-product items emit-text ul-close ] ");
    put(" each-product: func [items] [ emit-product-at items 0 block-len items ] ");
    put(" emit-product-at: func [items i n] [ either < i n [ emit-text li-open emit-text get block-at items i emit-text li-close emit-product-at items + i 1 n ] [ none ] ] ");
    /* application */
    put(" visit-count: 0 cart-count: 0 search-term: [] ");
    put(" catalog: [ "); put(hoist("Tea")); put(" "); put(hoist("Rice")); put(" ] ");
    put(" home-view: [ heading [ emit-text "); put(hoist("Glon Shop"));
    put(" ] link [ emit-text "); put(hoist("View products")); put(" ] "); put(hoist("products")); put(" ] ");
    put(" products-view: [ heading [ emit-text "); put(hoist("Products"));
    put(" ] list catalog text [ emit-text "); put(hoist("Products page visits: "));
    put(" emit-int visit-count ] text [ emit-text "); put(hoist("Search: "));
    put(" emit-text search-term ] input "); put(hoist("search"));
    put(" button [ emit-text "); put(hoist("Search")); put(" ] "); put(hoist("search"));
    put(" text [ emit-text "); put(hoist("Cart: "));
    put(" emit-int cart-count ] button [ emit-text "); put(hoist("Add")); put(" ] "); put(hoist("add-one"));
    put(" link [ emit-text "); put(hoist("Back home")); put(" ] "); put(hoist("home")); put(" ] ");
    put(" not-found-view: [ heading [ emit-text "); put(hoist("Not found"));
    put(" ] link [ emit-text "); put(hoist("Back home")); put(" ] "); put(hoist("home")); put(" ] ");
    put(" render-home: func [] [ emit-clear do home-view emit-finish ] ");
    put(" render-products: func [] [ emit-clear do products-view emit-finish ] ");
    put(" render-not-found: func [] [ emit-clear do not-found-view emit-finish ] ");
    put(" render-current: func [] [ either = current-route 'products [ render-products ] [ either = current-route 'home [ render-home ] [ render-not-found ] ] ] ");
    put(" go-home: func [] [ render-current ] ");
    put(" go-products: func [] [ visit-count: + visit-count 1 render-current ] ");
    put(" go-not-found: func [] [ render-current ] ");
    put(" route: [ either = current-route 'products [ go-products ] [ either = current-route 'home [ go-home ] [ go-not-found ] ] ] ");
    put(" add-one: func [] [ cart-count: + cart-count 1 render-current ] ");
    put(" search: func [] [ search-term: current-value render-current ] ");
    put(" do-event: [ either = current-event 'add-one [ add-one ] [ either = current-event 'search [ search ] [ render-current ] ] ] ");

    static char final_buf[65536];
    snprintf(final_buf, sizeof final_buf, "[ %s %s %s ]", prim_buf, defs_buf, prog_buf);
    return final_buf;
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

static const char *products_html(int visits, int cart, const char *term) {
    static char buf[1024];
    snprintf(buf, sizeof buf,
        "<h1>Products</h1><ul><li>Tea</li><li>Rice</li></ul>"
        "<p>Products page visits: %d</p><p>Search: %s</p>"
        "<input data-glon-input='search'>"
        "<button data-glon-event='search'>Search</button>"
        "<p>Cart: %d</p><button data-glon-event='add-one'>Add</button>"
        "<button data-glon-route='home'>Back home</button>", visits, term, cart);
    return buf;
}

int run_r0_s1_g1d_tests(void) {
    printf("g1d: 1 load once, products shows empty search\n");
    r0_s1_init();
    int err = 0;
    cell block = r0_s1_parse(build_program(), &err);
    r0_s1_run(block);
    const char *r = route("products");
    CHECK(r && strcmp(r, products_html(1, 0, "")) == 0, "1: products -> visits 1, cart 0, search empty");
    if (r && strcmp(r, products_html(1, 0, ""))) printf("     got: %s\n", r);

    printf("g1d: 2 search event with value\n");
    r = event_value("search", "green tea");
    CHECK(r && strcmp(r, products_html(1, 0, "green tea")) == 0, "2: search 'green tea' -> term shown, state unchanged otherwise");
    if (r && strcmp(r, products_html(1, 0, "green tea"))) printf("     got: %s\n", r);

    printf("g1d: 3 search event with another value\n");
    r = event_value("search", "rice");
    CHECK(r && strcmp(r, products_html(1, 0, "rice")) == 0, "3: search 'rice' -> term replaced");

    printf("g1d: 4 navigate away and back; term persists\n");
    route("home");
    r = route("products");
    CHECK(r && strcmp(r, products_html(2, 0, "rice")) == 0, "4: products again -> visits 2, term 'rice' persists");

    printf("g1d: 5 empty value clears the term\n");
    r = event_value("search", "");
    CHECK(r && strcmp(r, products_html(2, 0, "")) == 0, "5: empty value -> term cleared");

    printf("g1d: 6 unknown event with a value falls back safely\n");
    r = event_value("bogus", "ignored");
    CHECK(r && strcmp(r, products_html(2, 0, "")) == 0, "6: unknown event -> current view (no change)");

    return failures;
}
