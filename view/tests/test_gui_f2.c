/* test_gui_f2.c -- GUI-F2 regressions: bounded GUI state, deep-recursion-free
 * lookups and focused keyboard/clipboard routing (see GUI-F2-REPORT.md).
 * Usage: test_gui_f2 ROOT CASE
 *   widgets focus updates nofocus multifield clipboard destroy gc transport */
#include "gv_glon_host.h"
#include "gv_view.h"
#include "gv_backend.h"
#include "r0_s1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static gv_view *v;
static gv_backend *b;
static gv_glon_host *h;

#define NF 40   /* text fields, keys 1000.. */
#define NB 180  /* buttons, keys 2000.. */
#define K_TAB 9
#define K_SHIFT_TAB 1024
#define K_LEFT 1073741904
#define K_RIGHT 1073741903
#define K_HOME 1073741898
#define K_END 1073741901
#define K_DELETE 127
#define K_BACKSPACE 8
#define M_SHIFT 1u
#define M_CTRL 64u

static long gint(const char *expr);
/* the headless backend has no clipboard; the test supplies a plain one */
static char test_clip[4096];
static int test_set_clip(gv_backend *bb, const char *text) {
    (void)bb; snprintf(test_clip, sizeof test_clip, "%s", text); return 0;
}
static int test_get_clip(gv_backend *bb, char *buf, int cap) {
    (void)bb; snprintf(buf, (size_t)cap, "%s", test_clip); return 0;
}

static int setup(const char *root, int fields, int buttons) {
    v = gv_view_new(900, 700);
    b = gv_backend_headless();
    gv_backend_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.width = 900; cfg.height = 700; cfg.headless = 1;
    if (!b || b->init(b, &cfg) != 0 || b->create_window(b, 900, 700, "f2") != 0) return -1;
    b->set_clipboard = test_set_clip;
    b->get_clipboard = test_get_clip;
    h = gv_glon_host_new(v, b);
    if (!h || gv_glon_host_load_gui3_app(h, root, "view/tests/fixtures/gui_f2_app.glon") != 0) return -1;
    char src[128];
    /* small fixtures (the destroy case) are built and rebuilt in one event */
    snprintf(src, sizeof src, "[ f2/fields: %d f2/buttons: %d f2/first: %d ]", fields, buttons,
             fields + buttons <= 60 ? fields + buttons : 20);
    if (gv_glon_host_run(h, src) != 0) return -1;
    if (gv_glon_host_build_app(h, NULL) != 0 || gv_glon_host_error(h)) return -1;
    /* the rest arrives in later events of 40 fields each */
    while (gint("f2/made") < fields + buttons) {
        if (gv_glon_host_run(h, "[ f2/more 20 ]") != 0) {
            cell t, i, a; char ts[64] = "", is[64] = "", as[64] = "";
            if (r0_s1_uncaught_error(&t, &i, &a)) { r0_s1_mold(t, ts, 64); r0_s1_mold(i, is, 64); r0_s1_mold(a, as, 64); }
            fprintf(stderr, "f2/more failed at made=%ld view/id=%ld sin=%s/%s/%s\n", gint("f2/made"), gint("view/id"), ts, is, as);
            return -1;
        }
        if (gv_glon_host_key(h, 27) != 0 || gv_glon_host_error(h)) return -1;
    }
    return 0;
}

static long gint(const char *expr) {
    char buf[64] = "";
    if (gv_glon_host_text(h, expr, buf, sizeof buf) != 0) return -999999;
    return atol(buf);
}
static void gstr(const char *expr, char *out, int cap) {
    out[0] = 0;
    gv_glon_host_text(h, expr, out, cap);
}
static int node_of(int key) {
    char e[64]; snprintf(e, sizeof e, "f2/node %d", key); return (int)gint(e);
}
static void text_of(int key, char *out, int cap) {
    char e[64]; snprintf(e, sizeof e, "f2/text %d", key); gstr(e, out, cap);
}
static int focus_node(void) { return (int)gint("gui3/current-node"); }
static int key(int k) { return gv_glon_host_key(h, k); }
static int keym(int k, unsigned m) { return gv_glon_host_key_mods(h, k, m); }
static int type(const char *s) { return gv_glon_host_char_event(h, s); }
static int focus_key(int k) { return gv_glon_host_focus(h, node_of(k)); }
static long live_after_gc(void) {
    gv_glon_host_run(h, "[ gc/collect ]");
    return (long)r0_s1_gc_live_cells();
}
static int view_has_text(const char *t) {
    for (int i = 1; i <= v->count; i++) {
        gv_node *n = gv_view_node(v, i);
        if (n && n->text && !strcmp(n->text, t)) return 1;
    }
    return 0;
}

/* 1 + 10: more than 200 widgets; one record each; every View node built
 * through the by-reference transport */
static void case_widgets(void) {
    CHECK(gint("f2/fields") == NF && gint("f2/buttons") == NB);
    CHECK(gint("view/id") > 5 * NF + 2 * NB);
    long states = gint("f2/states");
    /* root container + label + box + NF fields + NB buttons */
    CHECK(states == 3 + NF + NB);
    CHECK(gint("f2/edits") == NF);
    int missing = 0;
    for (int k = 1000; k < 1000 + NF; k++) if (node_of(k) <= 0 || !gv_glon_host_view_node(h, node_of(k))) missing++;
    for (int k = 2000; k < 2000 + NB; k++) if (node_of(k) <= 0) missing++;
    CHECK(missing == 0);
    CHECK(view_has_text("f0") && view_has_text("f39") && view_has_text("b179"));
    int takes = 0, chunks = 0, errors = 0;
    gv_glon_host_f1b_stats(h, &takes, &chunks, &errors);
    printf("widgets: states=%ld edits=%ld view_id=%ld f1b takes=%d chunks=%d errors=%d\n",
           states, gint("f2/edits"), gint("view/id"), takes, chunks, errors);
    CHECK(chunks >= 10 && errors == 0);              /* > 40 KB of commands */
}

/* 2 + 3: hundreds of focus changes walk the fields in creation order and
 * leave the registry and the live heap bounded */
static void case_focus(void) {
    long states0 = gint("f2/states");
    CHECK(key(K_TAB) == 0);
    CHECK(focus_node() == node_of(1000));              /* creation order */
    int ok_order = 1;
    for (int i = 1; i < NF + NB; i++) {
        if (key(K_TAB) != 0) { ok_order = 0; break; }
        int expect = i < NF ? node_of(1000 + i) : node_of(2000 + i - NF);
        if (focus_node() != expect) { ok_order = 0; fprintf(stderr, "tab %d: focus %d expect %d\n", i, focus_node(), expect); break; }
    }
    CHECK(ok_order);
    /* every face has now been focused and blurred once; from here on the
     * live heap must not grow with further focus changes */
    long live0 = live_after_gc();
    CHECK(key(K_TAB) == 0);                             /* wraps to the first */
    CHECK(focus_node() == node_of(1000));
    CHECK(key(K_SHIFT_TAB) == 0);                       /* back wraps to the last */
    CHECK(focus_node() == node_of(2000 + NB - 1));
    for (int i = 0; i < 60; i++) CHECK(key(K_SHIFT_TAB) == 0);
    CHECK(focus_node() == node_of(2000 + NB - 61));
    for (int i = 0; i < 60; i++) CHECK(key(K_TAB) == 0);
    CHECK(focus_node() == node_of(2000 + NB - 1));
    long states1 = gint("f2/states");
    long live1 = live_after_gc();
    printf("focus: %d focus changes; states %ld -> %ld; live cells after GC %ld -> %ld\n",
           NF + NB + 122, states0, states1, live0, live1);
    CHECK(states1 == states0);
    CHECK(live1 - live0 < 200 && live0 - live1 < 200);  /* bounded: no growth */
    CHECK(!gv_glon_host_error(h));
}

/* 3: repeated updates of the same faces replace records */
static void case_updates(void) {
    long states0 = gint("f2/states");
    char warm[160];
    snprintf(warm, sizeof warm, "[ gui3/pointer-move %d gui3/pointer-move %d gui3/set-enabled %d 0 gui3/set-enabled %d 1 ]",
             node_of(2003), node_of(1005), node_of(2003), node_of(2003));
    CHECK(gv_glon_host_run(h, warm) == 0);
    CHECK(key(27) == 0);
    long live0 = live_after_gc();
    int a = node_of(1005), bnode = node_of(2003);
    char src[160];
    for (int i = 0; i < 300; i++) {
        snprintf(src, sizeof src, "[ gui3/pointer-move %d gui3/pointer-move %d gui3/set-enabled %d %d ]",
                 bnode, a, bnode, i & 1);
        CHECK(gv_glon_host_run(h, src) == 0);
        /* GUI-4B2C: hover now really changes on these moves.  The setup's
         * Escape (pointer-cancel) used to leave capture-key "", which sent
         * every later move down the captured path, so hover never changed.
         * Each run now queues more View commands; they are delivered every
         * 25 runs instead of piling up in one batch for 300. */
        if (i % 25 == 24) CHECK(key(27) == 0);
    }
    CHECK(gv_glon_host_run(h, "[ gui3/set-enabled f2/node 2003 1 ]") == 0);
    CHECK(key(27) == 0);              /* deliver the View commands those runs queued */
    long live1 = live_after_gc();
    printf("updates: 900 state replacements; states %ld -> %ld; live cells %ld -> %ld\n",
           states0, gint("f2/states"), live0, live1);
    CHECK(gint("f2/states") == states0);
    CHECK(live1 - live0 < 200 && live0 - live1 < 200);
}

/* 4: keys with no focused field do nothing and do not halt */
static void case_nofocus(void) {
    CHECK(focus_node() == 0);
    int keys[] = { K_LEFT, K_RIGHT, K_HOME, K_END, K_DELETE, K_BACKSPACE, 13, 32, 1021, 1022,
                   1073742049 /* Shift */, 27 };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) CHECK(key(keys[i]) == 0);
    CHECK(keym(97, M_CTRL) == 0 && keym(99, M_CTRL) == 0 && keym(120, M_CTRL) == 0);
    CHECK(type("Z") == 0);
    char t[64];
    int changed = 0;
    for (int k = 1000; k < 1000 + NF; k += 7) {
        text_of(k, t, sizeof t);
        char e[16]; snprintf(e, sizeof e, "f%d", k - 1000);
        if (strcmp(t, e)) changed++;
    }
    CHECK(changed == 0);
    CHECK(!gv_glon_host_error(h));
}

/* 5: typing, deletion and navigation go to the focused field only, with the
 * newest field never receiving input it was not given */
static void case_multifield(void) {
    char t[128];
    CHECK(focus_key(1003) == 0);
    CHECK(type("A") == 0 && type("B") == 0);           /* "f3AB" */
    CHECK(focus_key(1030) == 0);
    CHECK(type("x") == 0);                              /* "f30x" */
    CHECK(key(K_HOME) == 0 && key(K_DELETE) == 0);      /* "30x" */
    CHECK(key(K_END) == 0 && key(K_LEFT) == 0 && key(K_BACKSPACE) == 0); /* "3x" */
    CHECK(focus_key(1003) == 0);
    CHECK(key(K_BACKSPACE) == 0);                       /* "f3A" */
    text_of(1003, t, sizeof t); CHECK(!strcmp(t, "f3A"));
    text_of(1030, t, sizeof t); CHECK(!strcmp(t, "3x"));
    text_of(1039, t, sizeof t); CHECK(!strcmp(t, "f39"));   /* newest field untouched */
    text_of(1000, t, sizeof t); CHECK(!strcmp(t, "f0"));
    /* Tab from field 1003 to 1004, then type */
    CHECK(key(K_TAB) == 0);
    CHECK(focus_node() == node_of(1004));
    CHECK(type("!") == 0);
    text_of(1004, t, sizeof t); CHECK(!strcmp(t, "f4!"));
    /* a focused button takes no text */
    CHECK(focus_key(2001) == 0);
    CHECK(type("Q") == 0);
    text_of(1004, t, sizeof t); CHECK(!strcmp(t, "f4!"));
    text_of(1039, t, sizeof t); CHECK(!strcmp(t, "f39"));
}

/* 6: selection, copy, cut and paste stay with their own fields */
static void case_clipboard(void) {
    char t[128], clip[256];
    CHECK(focus_key(1010) == 0);                        /* "f10" */
    CHECK(keym(97, M_CTRL) == 0);                        /* select all in 1010 */
    CHECK(keym(99, M_CTRL) == 0);                        /* copy */
    CHECK(b->get_clipboard(b, clip, sizeof clip) == 0 && !strcmp(clip, "f10"));
    CHECK(focus_key(1020) == 0);                        /* "f20" */
    CHECK(keym(K_LEFT, M_SHIFT) == 0);                  /* select "0" in 1020 */
    /* field 1010 keeps its own full selection */
    CHECK(gint("block-at gui4/edit-get f2/node 1010 0") == 3);
    CHECK(gint("block-at gui4/edit-get f2/node 1010 1") == 0);
    CHECK(gint("block-at gui4/edit-get f2/node 1020 0") == 2);
    CHECK(gint("block-at gui4/edit-get f2/node 1020 1") == 3);
    CHECK(keym(118, M_CTRL) == 0);                       /* paste replaces "0" */
    text_of(1020, t, sizeof t); CHECK(!strcmp(t, "f2f10"));
    text_of(1010, t, sizeof t); CHECK(!strcmp(t, "f10"));
    CHECK(focus_key(1010) == 0);
    CHECK(key(K_END) == 0 && keym(K_LEFT, M_SHIFT) == 0 && keym(K_LEFT, M_SHIFT) == 0);
    CHECK(keym(120, M_CTRL) == 0);                       /* cut "10" from 1010 */
    CHECK(b->get_clipboard(b, clip, sizeof clip) == 0 && !strcmp(clip, "10"));
    text_of(1010, t, sizeof t); CHECK(!strcmp(t, "f"));
    text_of(1020, t, sizeof t); CHECK(!strcmp(t, "f2f10"));
    text_of(1039, t, sizeof t); CHECK(!strcmp(t, "f39"));
}

/* 7: destroying the container releases every record below it; rebuilding
 * creates fresh, working faces */
static void case_destroy(void) {
    long states0 = gint("f2/states");
    int old = node_of(1007);
    CHECK(focus_key(1007) == 0);
    CHECK(gv_glon_host_run(h, "[ gui3/destroy-face f2/box ]") == 0);
    CHECK(gint("f2/states") == states0 - 51);           /* box + 30 fields + 20 buttons */
    CHECK(gint("f2/edits") == 0);
    CHECK(node_of(1007) == 0);
    char e[64]; snprintf(e, sizeof e, "gui3/state %d", old); CHECK(gint(e) == 0);
    CHECK(focus_node() == 0);
    CHECK(key(K_TAB) == 0 && key(K_LEFT) == 0 && type("x") == 0);
    CHECK(gv_glon_host_run(h, "[ f2/fill ]") == 0);
    CHECK(type("y") == 0);                              /* pending batch delivered */
    CHECK(gint("f2/states") == states0);
    CHECK(gint("f2/edits") == 30);
    CHECK(node_of(1007) > old);
    CHECK(focus_key(1007) == 0 && type("Z") == 0);
    char t[64]; text_of(1007, t, sizeof t); CHECK(!strcmp(t, "f7Z"));
    for (int i = 0; i < 3; i++) CHECK(gv_glon_host_run(h, "[ f2/rebuild-box ]") == 0);
    CHECK(gint("f2/states") == states0);
    CHECK(gint("f2/edits") == 30);
}

/* 8: forced collections keep every live record */
static void case_gc(void) {
    char t[64];
    CHECK(focus_key(1012) == 0 && type("G") == 0 && keym(K_LEFT, M_SHIFT) == 0);
    long states0 = gint("f2/states");
    for (int i = 0; i < 4; i++) live_after_gc();
    CHECK(gint("f2/states") == states0);
    CHECK(gint("f2/edits") == NF);
    text_of(1012, t, sizeof t); CHECK(!strcmp(t, "f12G"));
    CHECK(gint("block-at gui4/edit-get f2/node 1012 0") == 3);
    CHECK(gint("block-at gui4/edit-get f2/node 1012 1") == 4);
    CHECK(type("H") == 0);                               /* replaces the selected "G" */
    text_of(1012, t, sizeof t); CHECK(!strcmp(t, "f12H"));
    text_of(1039, t, sizeof t); CHECK(!strcmp(t, "f39"));
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    const char *which = argc > 2 ? argv[2] : "widgets";
    int fields = NF, buttons = NB;
    if (!strcmp(which, "destroy")) { fields = 30; buttons = 20; }
    if (setup(root, fields, buttons) != 0) { fprintf(stderr, "setup failed\n"); return 2; }
    if (!strcmp(which, "widgets")) case_widgets();
    else if (!strcmp(which, "focus")) case_focus();
    else if (!strcmp(which, "updates")) case_updates();
    else if (!strcmp(which, "nofocus")) case_nofocus();
    else if (!strcmp(which, "multifield")) case_multifield();
    else if (!strcmp(which, "clipboard")) case_clipboard();
    else if (!strcmp(which, "destroy")) case_destroy();
    else if (!strcmp(which, "gc")) case_gc();
    else if (!strcmp(which, "big")) {         /* the large cases, in one build */
        case_widgets(); case_nofocus(); case_focus(); case_updates();
        case_multifield(); case_clipboard(); case_gc();
    }
    else { fprintf(stderr, "unknown case %s\n", which); return 2; }
    CHECK(!gv_glon_host_error(h));
    if (failures) { fprintf(stderr, "test_gui_f2 %s: %d failure(s)\n", which, failures); return 1; }
    printf("test_gui_f2 %s: ok\n", which);
    return 0;
}
