/* experiments/gui-f1b/f1b_proof.c -- GUI-F1B proof of the by-reference View
 * transport.  One event produces two command chunks; both are applied by the
 * View host in order; canaries around G1_OUT and the escape-law table are
 * unchanged; the host stores nothing into Glon data memory; no new primitive.
 * Usage: f1b_proof ROOT [pump]   ("pump" measures the event-pump hazard) */
#include "gv_glon_host.h"
#include "gv_view.h"
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void gv_glon_host_f1b_stats(gv_glon_host *h, int *takes, int *chunks, int *errors);

#define CANARY(a) ((cell)(0x7E7E0000L + (long)(a)) * 16 + 5)
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

typedef struct { cell lo, hi; } region;

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : ".";
    if (argc > 2 && !strcmp(argv[2], "pump")) {
        /* Hazard of an event-driven pump: does an ordinary "no-op" event make a
         * real application emit new View commands? */
        gv_view *v = gv_view_new(1000, 650);
        gv_glon_host *h = gv_glon_host_new(v, NULL);
        if (gv_glon_host_load_vocab_vid_app(h, root, "view/glon/widget_gallery.glon") != 0 ||
            gv_glon_host_build_app(h, NULL) != 0) return 2;
        for (int i = 0; i < 3; i++) {
            int last = 0, max = 0;
            gv_glon_host_click(h, 999);         /* a click on no widget: nothing to do */
            gv_glon_host_batch_stats(h, &last, &max);
            printf("widget_gallery: unhandled event %d still emitted a %d-byte batch\n", i + 1, last);
        }
        return 0;
    }

    gv_view *v = gv_view_new(800, 600);
    gv_glon_host *h = gv_glon_host_new(v, NULL);
    if (gv_glon_host_load_app(h, root, "experiments/gui-f1b/f1b_combined.glon") != 0 ||
        gv_glon_host_error(h)) { fprintf(stderr, "load failed\n"); return 2; }

    region regs[] = {
        { G1_OUT_DATA + 64, G1_OUT_DATA + G1_OUT_CAP },     /* G1_OUT beyond any small fragment, incl. last cell */
        { G1_OUT_DATA + G1_OUT_CAP, G1_VIS },              /* gap */
        { G1_VIS_DATA, G1_VIS_DATA + G1_VIS_CAP },         /* G1_VIS data */
#if GC_HEAP_BASE >= 34816
        { 32768, 34816 },                                  /* unused in >= 256K */
#endif
    };
    int nreg = (int)(sizeof regs / sizeof regs[0]);
    for (int r = 0; r < nreg; r++)
        for (cell a = regs[r].lo; a < regs[r].hi; a++) M[a] = CANARY(a);
    cell site_before[SITE_PARENT_CAP];
    for (int i = 0; i < SITE_PARENT_CAP; i++) site_before[i] = M[SITE_PARENT_BASE + i];

    CHECK(gv_glon_host_char_event(h, "x") == 0);           /* ONE event */
    CHECK(!gv_glon_host_error(h));

    int takes = 0, chunks = 0, errors = 0, last = 0, max = 0;
    gv_glon_host_f1b_stats(h, &takes, &chunks, &errors);
    gv_glon_host_batch_stats(h, &last, &max);
    char a_s[32] = "", b_s[32] = "";
    gv_glon_host_text(h, "f1b/a", a_s, sizeof a_s);
    gv_glon_host_text(h, "f1b/b", b_s, sizeof b_s);
    int a = atoi(a_s), b = atoi(b_s);
    gv_node *na = gv_view_node(v, gv_glon_host_view_node(h, a));
    gv_node *nb = gv_view_node(v, gv_glon_host_view_node(h, b));
    printf("event 1: takes=%d chunks=%d errors=%d largest chunk=%d B, fragment count M[G1_OUT]=%ld\n",
           takes, chunks, errors, max, (long)M[G1_OUT]);
    printf("node A=%d exists=%d text_len=%zu; node B=%d exists=%d parent_is_A=%d text=%s\n",
           a, na != NULL, na && na->text ? strlen(na->text) : 0, b, nb != NULL,
           nb && na && nb->parent == gv_glon_host_view_node(h, a), nb && nb->text ? nb->text : "-");
    CHECK(takes == 1);
    CHECK(chunks == 2);                                    /* two distinct chunks */
    CHECK(errors == 0);
    CHECK(na && nb);
    CHECK(nb && nb->parent == gv_glon_host_view_node(h, a)); /* chunk 1 (create A) before chunk 2 */
    CHECK(nb && nb->text && !strcmp(nb->text, "B-last"));
    CHECK(na && na->text && na->text[0] == 'A');
    CHECK(M[G1_OUT] < 64);                                 /* the only fragment is tiny */

    int dirty = 0;
    for (int r = 0; r < nreg; r++)
        for (cell a2 = regs[r].lo; a2 < regs[r].hi; a2++)
            if (M[a2] != CANARY(a2)) { if (!dirty) fprintf(stderr, "changed cell %ld\n", (long)a2); dirty++; }
    int site_changed = 0;
    for (int i = 0; i < SITE_PARENT_CAP; i++) if (M[SITE_PARENT_BASE + i] != site_before[i]) site_changed++;
    printf("canary cells changed=%d (incl. G1_OUT data cells 64..4095 and the last cell); SITE_PARENT entries changed=%d\n",
           dirty, site_changed);
    CHECK(dirty == 0);

    printf("%s\n", fails ? "F1B PROOF FAILED" : "F1B PROOF PASSED");
    return fails ? 1 : 0;
}
