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
        if (!*val && strcmp(key, "entitlements") != 0) continue;
        if (!strcmp(key, "name")) copy_field(app->pkg_name, sizeof app->pkg_name, val);
        else if (!strcmp(key, "version")) copy_field(app->version, sizeof app->version, val);
        else if (!strcmp(key, "entry")) copy_field(app->entry, sizeof app->entry, val);
        else if (!strcmp(key, "view")) copy_field(app->view, sizeof app->view, val);
        else if (!strcmp(key, "module")) list_add(app->modules, &app->nmodules, val);
        else if (!strcmp(key, "permission")) list_add(app->permissions, &app->npermissions, val);
        else if (!strcmp(key, "entitlement")) list_add(app->entitlements, &app->nentitlements, val);
    }
    fclose(f);
    return 0;
}

int glon_app_load_grants(const char *path,
                         char grants[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]) {
    int n = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
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
        if (!strcmp(key, "grant")) list_add(grants, &n, val);
    }
    fclose(f);
    return n;
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

int glon_app_resolve_write(const char *data_dir, const char *name,
                           char *out, int cap) {
    if (!name || !*name) return -1;
    if (strlen(name) > 200) return -1;
    /* basename only: no separators, no traversal, no drive/absolute names. */
    if (strchr(name, '/') || strchr(name, '\\')) return -1;
    if (strstr(name, "..")) return -1;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) return -1;
    if (strchr(name, ':')) return -1;
    for (const char *p = name; *p; p++)
        if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7f) return -1;
    if (strlen(data_dir) + strlen("/glon-fetch/downloads/") + strlen(name) + 1 > (size_t)cap)
        return -1;
    snprintf(out, cap, "%s/glon-fetch/downloads/%s", data_dir, name);
    return 0;
}
