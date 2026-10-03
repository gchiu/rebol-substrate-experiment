/* desktop/glon_app.h -- the Glon app model (host machinery, OS-independent).
 *
 * Parses an application manifest (modules / permissions / entitlements /
 * package identity), reads trusted grant state, computes effective authority,
 * resolves local modules, and constrains app-write paths.
 *
 * The manifest REQUESTS authority; it never grants any.  Effective authority
 * is: host implements INTERSECT manifest requests INTERSECT trusted grants.
 */
#ifndef GLON_APP_H
#define GLON_APP_H

#define GLON_APP_LIST_MAX 16
#define GLON_APP_NAME_MAX 64

typedef struct {
    char pkg_name[128];
    char version[32];
    char entry[256];
    char view[256];
    char modules[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]; int nmodules;
    char permissions[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]; int npermissions;
    char entitlements[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]; int nentitlements;
} glon_app_t;

/* Parse a manifest. Returns 0 on success, -1 on read error. */
int glon_app_manifest_load(const char *path, glon_app_t *app);

/* Read trusted grant lines ("grant <cap>"). Returns count, or -1 on error. */
int glon_app_load_grants(const char *path,
                         char grants[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]);

/* Effective = implements INTERSECT app->permissions INTERSECT grants.
 * impl is a NULL-terminated array. Returns count written to eff. */
int glon_app_effective(const glon_app_t *app,
                       char grants[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX],
                       int ngrants,
                       const char *const *impl,
                       char eff[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]);

/* Resolve a browser-supplied save name to an absolute path inside the
 * authorised app-write area: <data_dir>/glon-fetch/downloads/<name>.
 * Rejects separators, "..", absolute/drive names and empty names. 0/-1. */
int glon_app_resolve_write(const char *data_dir, const char *name,
                           char *out, int cap);

#endif /* GLON_APP_H */
