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
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char src[65536];
static char out[65536];

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
    if (!r0_s1_ran_cleanly()) return -4;   /* fail-stop HALT during load */
    return (N < 0) ? -3 : 0;
}

static const char *route(const char *token) {
    int out_len = 0;
    if (r0_s1_g1a_route(token, out, (int)sizeof out, &out_len) != 0) return NULL;
    if (!r0_s1_ran_cleanly()) return NULL;   /* fail-stop HALT during route */
    out[out_len] = 0;
    return out;
}

static const char *event(const char *token) {
    int out_len = 0;
    if (r0_s1_g1a_event(token, out, (int)sizeof out, &out_len) != 0) return NULL;
    if (!r0_s1_ran_cleanly()) return NULL;   /* fail-stop HALT during event */
    out[out_len] = 0;
    return out;
}

static const char *event_value(const char *token, const char *value) {
    int out_len = 0;
    if (r0_s1_g1a_event_value(token, value, out, (int)sizeof out, &out_len) != 0) return NULL;
    if (!r0_s1_ran_cleanly()) return NULL;   /* fail-stop HALT during event */
    out[out_len] = 0;
    return out;
}

static int has(const char *html, const char *needle) {
    return html != NULL && strstr(html, needle) != NULL;
}

/* Evaluate a single Glon form and return its integer result (0 on failure), so
 * a test can inspect live demo state -- e.g. the mutable delta blocks -- that
 * the rendered view never shows. */
static cell eval_int(const char *prog) {
    strcpy(src, prog);
    strip_comments(src);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) return 0;
    int N = r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly() || N != 1) return 0;
    return r0_s1_result(0, 1);
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

    {
        cell main_entry = r0_s1_init();
        /* Seed the M1 multitasking environment (the same cells the WASM
         * glon_init seeds) so the merchant-flow demo's worker tasks can run. */
        M[M1_MAIN_ENTRY_CELL] = main_entry;
        M[M1_CURSOR] = 0;
        for (int i = 0; i < M1_MAX_TASKS; i++)
            M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    }
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
    CHECK(has(r, "multitasking") && has(r, "data-glon-event='start'") &&
          has(r, "Worker 1") && has(r, "Router"),
          "9: home -> merchant-flow renders the flow graph + Start control");

    printf("g1e: merchant-flow runs three competing tasks with deterministic totals\n");

    r = event("start");
    CHECK(has(r, "data-glon-event='reset'") &&
          has(r, "Stock:  8") && has(r, "Cash:  -26") && has(r, "Sales:  2"),
          "10: Start runs the flow -> stock 8, cash -26, sales 2");
    CHECK(has(r, "Task 1 took BUY Tea") &&
          has(r, "Task 2 took SELL Tea") &&
          has(r, "Task 3 took RETURN Tea"),
          "11: the event log records which task took which job");
    CHECK(has(r, "Router -> inventory") &&
          has(r, "Router -> cash") &&
          has(r, "Router -> sales"),
          "12: the event log records the router fan-out");

    r = event("reset");
    CHECK(has(r, "data-glon-event='start'") &&
          has(r, "Stock:  0") && !has(r, "Task 1 took"),
          "13: Reset restores the initial state");

    r = event("start");
    CHECK(has(r, "Stock:  8") && has(r, "Cash:  -26") && has(r, "Sales:  2"),
          "14: Start again reproduces the same deterministic totals");

    route("home");
    r = route("merchant-flow");
    CHECK(has(r, "data-glon-event='reset'") && has(r, "Stock:  8"),
          "15: navigating away and back does not corrupt the machine");

    printf("g1e: merchant-flow reset genuinely zeroes the mutable blocks\n");

    /* start/reset/start/reset/start must stay deterministic, and each reset must
     * actually zero the mutable delta/worker blocks (not leave a stale mutated
     * loader literal behind). */
    r = event("start");
    CHECK(has(r, "Stock:  8") && has(r, "Cash:  -26") && has(r, "Sales:  2"),
          "15a: start -> stock 8 / cash -26 / sales 2");

    r = event("reset");
    CHECK(has(r, "Stock:  0") && has(r, "idle") && !has(r, "Task 1 took"),
          "15b: reset -> zero totals, idle workers, empty log");
    CHECK(int_val(eval_int("[ block-at stock-deltas 0 ]")) == 0 &&
          int_val(eval_int("[ block-at stock-deltas 1 ]")) == 0 &&
          int_val(eval_int("[ block-at stock-deltas 2 ]")) == 0 &&
          int_val(eval_int("[ block-at worker-job 0 ]")) == 0 &&
          int_val(eval_int("[ block-at worker-job 1 ]")) == 0 &&
          int_val(eval_int("[ block-at worker-job 2 ]")) == 0,
          "15c: mutable delta/worker blocks read back 0 after reset");

    r = event("start");
    CHECK(has(r, "Stock:  8") && has(r, "Cash:  -26") && has(r, "Sales:  2"),
          "15d: start -> stock 8 / cash -26 / sales 2");

    r = event("reset");
    CHECK(has(r, "Stock:  0") && has(r, "idle") && !has(r, "Task 1 took"),
          "15e: reset -> zero totals, idle workers, empty log");
    CHECK(int_val(eval_int("[ block-at stock-deltas 0 ]")) == 0 &&
          int_val(eval_int("[ block-at worker-job 2 ]")) == 0,
          "15f: mutable blocks read back 0 after the second reset");

    r = event("start");
    CHECK(has(r, "Stock:  8") && has(r, "Cash:  -26") && has(r, "Sales:  2") &&
          !r0_s1_stack_sentry_fired(),
          "15g: third start is deterministic and no sentry fires");

    printf("g1e: shop basket / events / persistence survive load-on-demand\n");

    r = route("products");
    CHECK(has(r, "0 items") && has(r, "data-glon-value='Tea'") && has(r, "data-glon-value='Rice'"),
          "16: products renders catalogue + empty basket");

    r = event_value("add-product", "Tea");
    CHECK(has(r, "Tea</span><span class='qty'>× 1</span>") && has(r, "1 item"),
          "17: Add Tea once => Tea x1");

    r = event_value("add-product", "Rice");
    CHECK(has(r, "Tea</span><span class='qty'>× 1</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "2 items"),
          "18: Add Rice once => Tea x1, Rice x1");

    r = event_value("add-product", "Tea");
    CHECK(has(r, "Tea</span><span class='qty'>× 2</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "3 items"),
          "19: Add Tea again => Tea x2, Rice x1, 3 items");

    route("home");
    r = route("products");
    CHECK(has(r, "Tea</span><span class='qty'>× 2</span>") &&
          has(r, "Rice</span><span class='qty'>× 1</span>") && has(r, "3 items"),
          "20: basket survives navigation away and back");

    r = event_value("search", "green tea");
    CHECK(has(r, "Search: green tea"), "21: search still works");

    printf("g1e: loaded STRING!/blocks/functions survive source-buffer reuse\n");
    /* `src` has been overwritten by guide.glon and merchant-flow.glon since
     * shop.glon was loaded; the shop's strings/blocks/closures must still be
     * intact (they were copied out of the source buffer at load time). */
    r = route("products");
    CHECK(has(r, "Glon Shop") || has(r, "Products"), "22: shop view intact after later loads");
    r = route("shop");
    CHECK(has(r, "Glon Shop"), "23: shop landing still renders after later loads");

    printf("g1e: failures and unknown routes are controlled\n");
    CHECK(has(route("definitely-unknown"), "Not found"), "24: unknown route still works");
    r = event("load-failed");
    CHECK(has(r, "Load failed"), "25: load-failed event renders a controlled error");

    printf("g1e: tuple-space demo (shared space + 3 workers + router)\n");

    CHECK(load_file("demo/shop/demos/tuple-space.glon") == 0, "26: tuple-space.glon loads");
    r = route("tuple-space");
    CHECK(has(r, "Transaction space") && has(r, "Worker") && has(r, "Router") &&
          has(r, "data-glon-event='run'"),
          "27: home -> tuple-space renders the space + workers + Run control");

    r = event("run");
    CHECK(has(r, "Stock:  8") && has(r, "Cash:  -26") && has(r, "Sales:  2"),
          "28: Run consumes the space -> stock 8, cash -26, sales 2");
    CHECK(has(r, "worker 1 took BUY Tea") && has(r, "worker 2 took SELL Tea") &&
          has(r, "worker 3 took RETURN Tea"),
          "29: more than one Glon worker consumed a tuple");
    CHECK(has(r, "worker 1 took") && has(r, "worker 2 took") && has(r, "worker 3 took") &&
          has(r, "Router -> inventory") && has(r, "Router -> cash") && has(r, "Router -> sales"),
          "30: all three tuples consumed exactly once + router fan-out");
    CHECK(!r0_s1_stack_sentry_fired(),
          "31: no SP/RP sentry fires during Run");

    r = event("space-reset");
    CHECK(has(r, "Stock:  0") && has(r, "pending"),
          "32: Reset clears the space and totals");

    r = event("run");
    CHECK(has(r, "Stock:  8") && has(r, "Cash:  -26") && has(r, "Sales:  2"),
          "33: rerunning reproduces the same deterministic totals");

    /* GC + task-slot reuse: run/reset/run again forces collections and reuses
     * the same three task slots; sentries must stay quiet throughout. */
    r = event("space-reset");
    r = event("run");
    CHECK(has(r, "Stock:  8") && !r0_s1_stack_sentry_fired(),
          "34: GC during/after task activity + slot reuse remain safe (no sentry)");

    route("home");
    r = route("tuple-space");
    CHECK(has(r, "Transaction space") && has(r, "data-glon-event='space-reset'"),
          "35: navigating away and back does not corrupt the demo");

    return failures;
}
