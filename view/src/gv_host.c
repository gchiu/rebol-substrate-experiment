/* gv_host.c -- the provider registry and the mock provider (FOREIGN-1.md).
 *
 * The mock provider renders nothing.  It is the component stand-in for the
 * dummy-driver tests: it records what the host told it (bounds, clip,
 * visibility, focus, commands) and can emit events as a user's input inside
 * a real component would.  The real demonstration component is x11-edit
 * (gv_host_x11.c). */

#include "gv_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GV_HOST_MAX_PROVIDERS 8
static const gv_host_provider *gv_providers[GV_HOST_MAX_PROVIDERS];
static int gv_nproviders;
static int gv_alive, gv_created, gv_destroyed;

extern const gv_host_provider gv_host_mock_provider;
extern const gv_host_provider gv_host_x11_provider;
extern const gv_host_provider gv_host_webkit_provider;

static void gv_host_builtins(void) {
    static int done;
    if (done) return;
    done = 1;
    gv_host_register(&gv_host_mock_provider);
    gv_host_register(&gv_host_x11_provider);
    gv_host_register(&gv_host_webkit_provider);     /* FOREIGN-2 */
}

int gv_host_register(const gv_host_provider *p) {
    if (!p || p->abi != GV_HOST_ABI || !p->kind || gv_nproviders >= GV_HOST_MAX_PROVIDERS) return -1;
    for (int i = 0; i < gv_nproviders; i++)
        if (!strcmp(gv_providers[i]->kind, p->kind)) { gv_providers[i] = p; return 0; }
    gv_providers[gv_nproviders++] = p;
    return 0;
}

const gv_host_provider *gv_host_find(const char *kind) {
    gv_host_builtins();
    for (int i = 0; i < gv_nproviders; i++)
        if (!strcmp(gv_providers[i]->kind, kind)) return gv_providers[i];
    return NULL;
}

void gv_host_counts(int *alive, int *created, int *destroyed) {
    if (alive) *alive = gv_alive;
    if (created) *created = gv_created;
    if (destroyed) *destroyed = gv_destroyed;
}
/* the providers call these on create / destroy */
void gv_host_count_create(void) { gv_alive++; gv_created++; }
void gv_host_count_destroy(void) { gv_alive--; gv_destroyed++; }

/* ---- the mock provider ------------------------------------------------------ */
typedef struct gv_mock {
    unsigned magic;
    gv_host_env env;
    gv_host_mock_state st;
} gv_mock;
#define GV_MOCK_MAGIC 0x6d6f636bu

static void *gv_mock_create(const gv_host_env *env, const char *props, char *err, int errcap) {
    (void)err; (void)errcap;
    gv_mock *m = (gv_mock *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->magic = GV_MOCK_MAGIC;
    m->env = *env;
    snprintf(m->st.text, sizeof m->st.text, "%s", props ? props : "");
    gv_host_count_create();
    return m;
}
static void gv_mock_place(void *inst, gv_rect bounds, gv_rect clip, int visible) {
    gv_mock *m = (gv_mock *)inst;
    m->st.bounds = bounds;
    m->st.clip = clip;
    m->st.visible = visible;
    m->st.places++;
}
static int gv_mock_focus(void *inst, int how) {
    gv_mock *m = (gv_mock *)inst;
    m->st.focused = how != 0;
    m->st.focus_how = how;
    m->st.focus_calls++;
    return 1;
}
static int gv_mock_command(void *inst, const char *cmd, const char *arg) {
    gv_mock *m = (gv_mock *)inst;
    m->st.commands++;
    snprintf(m->st.last_cmd, sizeof m->st.last_cmd, "%s", cmd);
    snprintf(m->st.last_arg, sizeof m->st.last_arg, "%s", arg ? arg : "");
    if (!strcmp(cmd, "set-text")) {
        snprintf(m->st.text, sizeof m->st.text, "%s", arg ? arg : "");
        return 0;
    }
    /* any other command: recorded (tests); "refuse" is refused, as a
     * component refuses a command it does not know (INTENT-3 tests) */
    return strcmp(cmd, "refuse") ? 0 : -1;
}
static int gv_mock_pump(void *inst) { (void)inst; return 0; }
static void gv_mock_destroy(void *inst) {
    gv_mock *m = (gv_mock *)inst;
    m->magic = 0;
    free(m);
    gv_host_count_destroy();
}

const gv_host_provider gv_host_mock_provider = {
    GV_HOST_ABI, "mock", gv_mock_create, gv_mock_place, gv_mock_focus, gv_mock_command, gv_mock_pump, gv_mock_destroy
};

int gv_host_mock_state_of(void *inst, gv_host_mock_state *out) {
    gv_mock *m = (gv_mock *)inst;
    if (!m || m->magic != GV_MOCK_MAGIC || !out) return -1;
    *out = m->st;
    return 0;
}
void gv_host_mock_emit(void *inst, const char *name, const char *detail) {
    gv_mock *m = (gv_mock *)inst;
    if (!m || m->magic != GV_MOCK_MAGIC) return;
    if (!strcmp(name, "change") && detail) snprintf(m->st.text, sizeof m->st.text, "%s", detail);
    m->env.emit(m->env.token, name, detail ? detail : "");
}
