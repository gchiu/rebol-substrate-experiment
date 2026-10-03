/* desktop/glon_app.h -- the Glon app model (host machinery, OS-independent).
 *
 * Parses an application manifest (stable id / modules / permissions /
 * entitlements / package identity), reads trusted per-application grant state,
 * computes effective authority for THIS application id only, resolves local
 * modules, and constrains app-write paths to identity-scoped storage.
 *
 * The manifest REQUESTS authority; it never grants any.  Effective authority:
 *
 *     HOST IMPLEMENTS
 *       INTERSECT PLATFORM ALLOWS
 *       INTERSECT APPLICATION REQUESTS
 *       INTERSECT USER/POLICY GRANTS FOR THIS APP ID
 *       = APPLICATION GETS
 */
#ifndef GLON_APP_H
#define GLON_APP_H

#define GLON_APP_LIST_MAX 16
#define GLON_APP_NAME_MAX 64
#define GLON_APP_ID_MAX 128
#define GLON_GRANT_APPS_MAX 16
#define GLON_CAT_MAX 32
#define GLON_PURPOSE_TEXT_MAX 256

/* Trusted capability catalogue entry: canonical semantics, defined by the
 * trusted host, never by an application. */
typedef struct {
    char id[GLON_APP_NAME_MAX];
    char klass[32];
    char label[128];
    char risk[16];
    char allows[64];
} glon_capability_t;

typedef struct {
    glon_capability_t caps[GLON_CAT_MAX];
    int ncaps;
} glon_catalogue_t;

/* Application-supplied purpose: an untrusted vendor claim about WHY it wants a
 * permission.  It never affects canonical semantics or authority. */
typedef struct {
    char permission[GLON_APP_NAME_MAX];
    char text[GLON_PURPOSE_TEXT_MAX];
} glon_purpose_t;

typedef struct {
    char app_id[GLON_APP_ID_MAX];  /* stable authority principal (mandatory) */
    char pkg_name[128];            /* display metadata; no authority meaning */
    char version[32];              /* not part of the D8 authority principal */
    char entry[256];
    char view[256];
    char modules[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]; int nmodules;
    char permissions[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]; int npermissions;
    char entitlements[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]; int nentitlements;
    glon_purpose_t purposes[GLON_APP_LIST_MAX]; int npurposes;
} glon_app_t;

typedef struct {
    char app_id[GLON_APP_ID_MAX];
    char perms[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX];
    int nperms;
} glon_grant_app_t;

typedef struct {
    glon_grant_app_t apps[GLON_GRANT_APPS_MAX];
    int napps;
} glon_grants_t;

/* Valid application id: two or more dot-separated segments; each segment is
 * [a-z][a-z0-9-]* and does not end with '-'.  Exact, case-sensitive, no
 * prefix/substring semantics. */
int glon_app_id_valid(const char *id);

/* ---- D9: trusted capability catalogue ------------------------------------ */

/* Load the trusted capability catalogue.  Returns 0 on success, -1 on read
 * error.  This is trusted host/Glon metadata, outside every application. */
int glon_catalogue_load(const char *path, glon_catalogue_t *cat);

/* Exact lookup.  Returns NULL when the capability is unknown (fail closed). */
const glon_capability_t *glon_catalogue_find(const glon_catalogue_t *cat, const char *id);

/* Application-supplied purpose for a requested permission, or NULL. */
const char *glon_app_purpose(const glon_app_t *app, const char *permission);

/* Pure permission-set comparison for a future signed-update delta.  Any output
 * array may be NULL.  Returns 0. */
int glon_perms_diff(const char oldp[][GLON_APP_NAME_MAX], int nold,
                    const char newp[][GLON_APP_NAME_MAX], int nnew,
                    char added[][GLON_APP_NAME_MAX], int *nadded,
                    char removed[][GLON_APP_NAME_MAX], int *nremoved,
                    char unchanged[][GLON_APP_NAME_MAX], int *nunchanged);

/* Parse a manifest.  Returns 0 on success, -1 on read error, -2 on a missing
 * or malformed mandatory id. */
int glon_app_manifest_load(const char *path, glon_app_t *app);

/* Read the trusted per-application grant store.  Returns 0 on success, -1 on
 * read error.  Grants appear under "app <id>" blocks. */
int glon_app_load_grants(const char *path, glon_grants_t *g);

/* Trusted installation state: resolve an installed application id to its
 * package directory.  Exact strcmp matching on the id only.  Returns 0 and
 * writes the directory, or -1 if the id is not installed. */
int glon_install_resolve(const char *registry_path, const char *app_id,
                         char *out_dir, int cap);

/* Copy the granted permissions for exactly this app id into out; returns the
 * count.  Exact strcmp matching only -- no wildcard/prefix/global fallback. */
int glon_app_grants_for(const glon_grants_t *g, const char *app_id,
                        char out[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]);

/* Effective = implements INTERSECT app->permissions INTERSECT this app's grants.
 * impl is a NULL-terminated array.  Returns count written to eff. */
int glon_app_effective(const glon_app_t *app,
                       char grants[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX],
                       int ngrants,
                       const char *const *impl,
                       char eff[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX]);

/* Resolve a browser-supplied save name to identity-scoped storage:
 *   <data_dir>/<app_id>/downloads/<name>.
 * Rejects separators, "..", absolute/drive names, empty names and any app_id
 * that is not a valid application id.  0/-1. */
int glon_app_resolve_write(const char *data_dir, const char *app_id,
                           const char *name, char *out, int cap);

#endif /* GLON_APP_H */
