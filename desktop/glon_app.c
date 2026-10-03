/* desktop/glon_app.c -- implementation of the Glon app model. */

#include "glon_app.h"

#include <stdio.h>
#include <string.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n'))
        s[--n] = 0;
    return s;
}

static void copy_field(char *dst, int cap, const char *src) {
    int n = 0;
    while (src[n] && n < cap - 1) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

static int list_add(char list[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX], int *n, const char *v) {
    if (*n >= GLON_APP_LIST_MAX) return -1;
    copy_field(list[*n], GLON_APP_NAME_MAX, v);
    (*n)++;
    return 0;
}

static int list_has(char list[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX], int n, const char *v) {
    for (int i = 0; i < n; i++) if (strcmp(list[i], v) == 0) return 1;
    return 0;
}

/* id := segment ('.' segment)+ ; segment := [a-z][a-z0-9-]*, no trailing '-'. */
int glon_app_id_valid(const char *id) {
    if (!id || !*id || strlen(id) > GLON_APP_ID_MAX - 1) return 0;
    int segments = 0;
    const char *p = id;
    for (;;) {
        if (!(*p >= 'a' && *p <= 'z')) return 0;     /* segment starts with letter */
        const char *start = p;
        while ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-') p++;
        if (p == start || p[-1] == '-') return 0;    /* non-empty, no trailing '-' */
        segments++;
        if (*p == '.') { p++; if (!*p) return 0; continue; }   /* no trailing dot */
        if (*p == 0) break;
        return 0;                                    /* invalid character */
    }
    return segments >= 2;                            /* at least two segments */
}

static int grants_find_or_add(glon_grants_t *g, const char *id) {
    for (int i = 0; i < g->napps; i++)
        if (strcmp(g->apps[i].app_id, id) == 0) return i;
    if (g->napps >= GLON_GRANT_APPS_MAX) return -1;
    int i = g->napps++;
    memset(&g->apps[i], 0, sizeof g->apps[i]);
    copy_field(g->apps[i].app_id, GLON_APP_ID_MAX, id);
    return i;
}

/* ---- D9: trusted capability catalogue ------------------------------------ */

static char *unquote(char *s) {
    int n = (int)strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') { s[n - 1] = 0; return s + 1; }
    return s;
}

int glon_catalogue_load(const char *path, glon_catalogue_t *cat) {
    memset(cat, 0, sizeof *cat);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int cur = -1;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s = trim(line);
        if (!*s) continue;
        char *sp = strchr(s, ' ');
        if (!sp) continue;
        *sp = 0;
        char *key = trim(s);
        char *val = trim(sp + 1);
        if (strcmp(key, "capability") == 0) {
            if (cat->ncaps >= GLON_CAT_MAX) { cur = -1; continue; }
            cur = cat->ncaps++;
            memset(&cat->caps[cur], 0, sizeof cat->caps[cur]);
            copy_field(cat->caps[cur].id, GLON_APP_NAME_MAX, val);
        } else if (cur >= 0) {
            if (strcmp(key, "class") == 0) copy_field(cat->caps[cur].klass, sizeof cat->caps[cur].klass, val);
            else if (strcmp(key, "label") == 0) copy_field(cat->caps[cur].label, sizeof cat->caps[cur].label, unquote(val));
            else if (strcmp(key, "risk") == 0) copy_field(cat->caps[cur].risk, sizeof cat->caps[cur].risk, val);
            else if (strcmp(key, "allows") == 0) copy_field(cat->caps[cur].allows, sizeof cat->caps[cur].allows, val);
        }
    }
    fclose(f);
    return 0;
}

const glon_capability_t *glon_catalogue_find(const glon_catalogue_t *cat, const char *id) {
    for (int i = 0; i < cat->ncaps; i++)
        if (strcmp(cat->caps[i].id, id) == 0) return &cat->caps[i];
    return NULL;
}

const char *glon_app_purpose(const glon_app_t *app, const char *permission) {
    for (int i = 0; i < app->npurposes; i++)
        if (strcmp(app->purposes[i].permission, permission) == 0) return app->purposes[i].text;
    return NULL;
}

static int perms_has(char list[][GLON_APP_NAME_MAX], int n, const char *v) {
    for (int i = 0; i < n; i++) if (strcmp(list[i], v) == 0) return 1;
    return 0;
}

int glon_perms_diff(const char oldp[][GLON_APP_NAME_MAX], int nold,
                    const char newp[][GLON_APP_NAME_MAX], int nnew,
                    char added[][GLON_APP_NAME_MAX], int *nadded,
                    char removed[][GLON_APP_NAME_MAX], int *nremoved,
                    char unchanged[][GLON_APP_NAME_MAX], int *nunchanged) {
    int na = 0, nr = 0, nu = 0;
    for (int i = 0; i < nold; i++) {
        if (perms_has((char (*)[GLON_APP_NAME_MAX])newp, nnew, oldp[i])) {
            if (unchanged) copy_field(unchanged[nu], GLON_APP_NAME_MAX, oldp[i]);
            nu++;
        } else {
            if (removed) copy_field(removed[nr], GLON_APP_NAME_MAX, oldp[i]);
            nr++;
        }
    }
    for (int i = 0; i < nnew; i++) {
        if (!perms_has((char (*)[GLON_APP_NAME_MAX])oldp, nold, newp[i])) {
            if (added) copy_field(added[na], GLON_APP_NAME_MAX, newp[i]);
            na++;
        }
    }
    if (nadded) *nadded = na;
    if (nremoved) *nremoved = nr;
    if (nunchanged) *nunchanged = nu;
    return 0;
}

int glon_app_manifest_load(const char *path, glon_app_t *app) {
    memset(app, 0, sizeof *app);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s = trim(line);
        if (!*s) continue;
        char *colon = strchr(s, ':');
        if (!colon) continue;
        *colon = 0;
        char *key = trim(s);
        char *val = trim(colon + 1);
        if (!strcmp(key, "id")) copy_field(app->app_id, sizeof app->app_id, val);
        else if (!strcmp(key, "name")) copy_field(app->pkg_name, sizeof app->pkg_name, val);
        else if (!strcmp(key, "version")) copy_field(app->version, sizeof app->version, val);
        else if (!strcmp(key, "entry")) copy_field(app->entry, sizeof app->entry, val);
        else if (!strcmp(key, "view")) copy_field(app->view, sizeof app->view, val);
        else if (!strcmp(key, "module")) list_add(app->modules, &app->nmodules, val);
        else if (!strcmp(key, "permission")) list_add(app->permissions, &app->npermissions, val);
        else if (!strcmp(key, "entitlement")) list_add(app->entitlements, &app->nentitlements, val);
        else if (!strcmp(key, "purpose")) {
            /* "purpose: <permission-id> <free-text explanation>".
             * First purpose for a given permission wins; later duplicates are
             * ignored deterministically.  Purpose text is an untrusted claim
             * and never affects canonical semantics or authority. */
            char perm[GLON_APP_NAME_MAX] = {0};
            const char *text = "";
            char *sp = strchr(val, ' ');
            if (sp) { *sp = 0; text = trim(sp + 1); }
            copy_field(perm, sizeof perm, val);
            if (*perm && !glon_app_purpose(app, perm) && app->npurposes < GLON_APP_LIST_MAX) {
                copy_field(app->purposes[app->npurposes].permission, GLON_APP_NAME_MAX, perm);
                copy_field(app->purposes[app->npurposes].text, GLON_PURPOSE_TEXT_MAX, text);
                app->npurposes++;
            }
        }
    }
    fclose(f);
    if (!glon_app_id_valid(app->app_id)) return -2;   /* id is mandatory + valid */
    return 0;
}

int glon_app_load_grants(const char *path, glon_grants_t *g) {
    memset(g, 0, sizeof *g);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int current = -1;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s = trim(line);
        if (!*s) continue;
        char *sp = strchr(s, ' ');
        if (!sp) continue;
        *sp = 0;
        char *key = trim(s);
        char *val = trim(sp + 1);
        if (!strcmp(key, "app")) {
            current = glon_app_id_valid(val) ? grants_find_or_add(g, val) : -1;
        } else if (!strcmp(key, "grant")) {
            if (current >= 0) list_add(g->apps[current].perms, &g->apps[current].nperms, val);
        }
    }
    fclose(f);
    return 0;
}

/* Trusted installation registry: "install <app-id> <package-dir>" lines.
 * The registry, not the manifest, binds identity to an installed package. */
int glon_install_resolve(const char *registry_path, const char *app_id,
                         char *out_dir, int cap) {
    FILE *f = fopen(registry_path, "rb");
    if (!f) return -1;
    char line[1024];
    int found = -1;
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s = trim(line);
        if (!*s) continue;
        char *sp = strchr(s, ' ');
        if (!sp) continue;
        *sp = 0;
        char *key = trim(s);
        char *rest = trim(sp + 1);
        if (strcmp(key, "install") != 0) continue;
        char *sp2 = strchr(rest, ' ');
        if (!sp2) continue;
        *sp2 = 0;
        char *id = trim(rest);
        char *dir = trim(sp2 + 1);
        if (strcmp(id, app_id) == 0) { copy_field(out_dir, cap, dir); found = 0; break; }
    }
    fclose(f);
    return found;
}

int glon_app_grants_for(const glon_grants_t *g, const char *app_id,
                        char out[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]) {
    for (int i = 0; i < g->napps; i++) {
        if (strcmp(g->apps[i].app_id, app_id) == 0) {
            int n = g->apps[i].nperms;
            if (n > GLON_APP_LIST_MAX) n = GLON_APP_LIST_MAX;
            for (int j = 0; j < n; j++) copy_field(out[j], GLON_APP_NAME_MAX, g->apps[i].perms[j]);
            return n;
        }
    }
    return 0;   /* unknown id or no block -> nothing granted */
}

int glon_app_effective(const glon_app_t *app,
                       char grants[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX],
                       int ngrants,
                       const char *const *impl,
                       char eff[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]) {
    int n = 0;
    for (int i = 0; impl[i]; i++) {
        if (list_has((char (*)[GLON_APP_NAME_MAX])app->permissions, app->npermissions, impl[i]) &&
            list_has(grants, ngrants, impl[i])) {
            if (n < GLON_APP_LIST_MAX) copy_field(eff[n], GLON_APP_NAME_MAX, impl[i]);
            n++;
        }
    }
    return n;
}

int glon_app_resolve_write(const char *data_dir, const char *app_id,
                           const char *name, char *out, int cap) {
    if (!glon_app_id_valid(app_id)) return -1;
    if (!name || !*name) return -1;
    if (strlen(name) > 200) return -1;
    /* basename only: no separators, no traversal, no drive/absolute names. */
    if (strchr(name, '/') || strchr(name, '\\')) return -1;
    if (strstr(name, "..")) return -1;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) return -1;
    if (strchr(name, ':')) return -1;
    for (const char *p = name; *p; p++)
        if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7f) return -1;
    if (strlen(data_dir) + 1 + strlen(app_id) + strlen("/downloads/") + strlen(name) + 1 > (size_t)cap)
        return -1;
    snprintf(out, cap, "%s/%s/downloads/%s", data_dir, app_id, name);
    return 0;
}
