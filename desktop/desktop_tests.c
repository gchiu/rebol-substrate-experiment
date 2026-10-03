/* desktop/desktop_tests.c -- headless proof of the desktop application split.
 *
 * Runs the real Glon sources through the real native dispatchers (no sockets,
 * no browser):
 *   Phase A: desktop/app.glon -- the native application asks for exactly the
 *            file it decided on (fs-read), and builds the response body from
 *            the bytes the filesystem returned (http-respond).
 *   Phase B: desktop/view.glon -- the browser Glon View renders its button and
 *            renders the reply into the DOM fragment.
 *
 * No WASM/emcc/node is required; the browser renderer (r0_s1_g1a.c) is the
 * same browser-agnostic C used inside the WASM module.
 */

#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include "r0_s1_g1a_live.h"
#include "m1_layout.h"
#include "glon_app.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char filebuf[65536];
static char wrapbuf[65536];

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "desktop-test: cannot open %s\n", path); return -1; }
    size_t n = fread(filebuf, 1, sizeof filebuf - 1, f);
    fclose(f);
    filebuf[n] = 0;
    char *r = filebuf, *w = filebuf;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;

    const char *src = filebuf;
    char *p = filebuf;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p && *p != '[') {
        size_t len = strlen(filebuf);
        if (len + 4 >= sizeof wrapbuf) return -2;
        wrapbuf[0] = '['; wrapbuf[1] = ' ';
        memcpy(wrapbuf + 2, filebuf, len);
        wrapbuf[len + 2] = ' ';
        wrapbuf[len + 3] = ']';
        wrapbuf[len + 4] = 0;
        src = wrapbuf;
    }

    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "desktop-test: parse error in %s (%d)\n", path, err); return -2; }
    r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "desktop-test: run failed in %s\n", path); return -3; }
    return 0;
}

static void seed_tasks(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}

static char got_op[256], got_arg[16384];
static int got_calls;

static void record_host_call(const char *op, int op_len,
                             const char *arg, int arg_len, void *user) {
    (void)user;
    if (op_len > (int)sizeof got_op - 1) op_len = (int)sizeof got_op - 1;
    if (arg_len > (int)sizeof got_arg - 1) arg_len = (int)sizeof got_arg - 1;
    memcpy(got_op, op, (size_t)op_len); got_op[op_len] = 0;
    memcpy(got_arg, arg, (size_t)arg_len); got_arg[arg_len] = 0;
    got_calls++;
}

static int dispatch_event_bytes(const char *token, const char *text,
                                char *out, int cap) {
    int out_len = 0;
    int rc = r0_s1_g1a_live_event_bytes(token, (const unsigned char *)text,
                                        (unsigned int)strlen(text), out, cap, &out_len);
    if (rc != 0) return -1;
    if (out_len < cap) out[out_len] = 0;
    return out_len;
}

static char outbuf[65536];

static void phase_native_app(void) {
    printf("native application logic (desktop/app.glon)\n");
    seed_tasks();
    CHECK(load_file("glon-lib/prelude.glon") == 0, "prelude loads");
    CHECK(load_file("glon-lib/strings.glon") == 0, "strings loads");
    CHECK(load_file("desktop/emit.glon") == 0, "emit library loads");
    CHECK(load_file("desktop/app.glon") == 0, "application loads");
    if (failures) return;

    r0_s1_g1a_live_set_host_call(record_host_call, 0);

    got_calls = 0; got_op[0] = got_arg[0] = 0;
    CHECK(dispatch_event_bytes("native-read", "greeting", outbuf, (int)sizeof outbuf) >= 0,
          "native-read dispatches");
    CHECK(got_calls == 1 && strcmp(got_op, "fs-read") == 0,
          "application requests the filesystem primitive");
    CHECK(strcmp(got_arg, "desktop/demo.txt") == 0,
          "application mapped 'greeting' -> desktop/demo.txt");

    got_calls = 0; got_op[0] = got_arg[0] = 0;
    CHECK(dispatch_event_bytes("native-read", "desktop/big.txt", outbuf, (int)sizeof outbuf) >= 0,
          "native-read with a path-like key dispatches");
    CHECK(got_calls == 1 && strcmp(got_op, "fs-read") == 0 && got_arg[0] == 0,
          "path-like key is not authorised (no filesystem path)");

    got_calls = 0; got_op[0] = got_arg[0] = 0;
    CHECK(dispatch_event_bytes("native-read", "../../etc/passwd", outbuf, (int)sizeof outbuf) >= 0,
          "native-read with a traversal key dispatches");
    CHECK(got_calls == 1 && strcmp(got_op, "fs-read") == 0 && got_arg[0] == 0,
          "traversal key is not authorised (no filesystem path)");

    static char content[65536];
    int clen = 0;
    FILE *f = fopen("desktop/demo.txt", "rb");
    if (f) { clen = (int)fread(content, 1, sizeof content - 1, f); fclose(f); content[clen] = 0; }
    CHECK(clen > 0, "filesystem primitive read demo.txt");

    got_calls = 0; got_op[0] = got_arg[0] = 0;
    CHECK(dispatch_event_bytes("fs-read-reply", content, outbuf, (int)sizeof outbuf) >= 0,
          "fs-read-reply dispatches");
    CHECK(got_calls == 1 && strcmp(got_op, "http-respond") == 0,
          "application builds the response after the read");
    CHECK(strcmp(got_arg, content) == 0,
          "response body is exactly the bytes the filesystem returned");

    got_calls = 0; got_op[0] = got_arg[0] = 0;
    CHECK(dispatch_event_bytes("resource-path", "large", outbuf, (int)sizeof outbuf) >= 0,
          "resource-path dispatches");
    CHECK(got_calls == 1 && strcmp(got_op, "serve-file") == 0,
          "application asks the host to stream the file (data plane)");
    CHECK(strcmp(got_arg, "desktop/big.txt") == 0,
          "application mapped resource 'large' -> desktop/big.txt");

    got_calls = 0; got_op[0] = got_arg[0] = 0;
    CHECK(dispatch_event_bytes("resource-path", "nope", outbuf, (int)sizeof outbuf) >= 0,
          "resource-path with an unknown name dispatches");
    CHECK(got_calls == 1 && strcmp(got_op, "serve-file") == 0 && got_arg[0] == 0,
          "unknown resource resolves to the empty path (host rejects)");
}

static void phase_browser_view(void) {
    printf("browser View logic (desktop/view.glon)\n");
    seed_tasks();
    CHECK(load_file("glon-lib/prelude.glon") == 0, "prelude reloads");
    CHECK(load_file("desktop/emit.glon") == 0, "emit reloads");
    CHECK(load_file("desktop/view.glon") == 0, "view loads");
    if (failures) return;

    int out_len = 0;
    outbuf[0] = 0;
    CHECK(r0_s1_g1a_route("home", outbuf, (int)sizeof outbuf, &out_len) == 0,
          "browser View routes 'home'");
    CHECK(strstr(outbuf, "data-glon-native='greeting'") != 0,
          "View asks for the logical file 'greeting'");
    CHECK(strstr(outbuf, "data-glon-event='native-reply'") != 0,
          "View names the reply event");
    CHECK(strstr(outbuf, "data-glon-event='select-large'") != 0,
          "View offers the bulk resource selection");

    outbuf[0] = 0;
    CHECK(r0_s1_g1a_event_value("native-reply", "HELLO FROM DISK", outbuf,
                                (int)sizeof outbuf, &out_len) == 0,
          "browser View handles the reply event");
    CHECK(strstr(outbuf, "HELLO FROM DISK") != 0,
          "View renders the reply into the DOM fragment");
    CHECK(strstr(outbuf, "Round trip complete") != 0,
          "View shows the completed round trip");

    outbuf[0] = 0;
    CHECK(r0_s1_g1a_event("select-large", outbuf, (int)sizeof outbuf, &out_len) == 0,
          "browser View handles select-large");
    CHECK(strstr(outbuf, "/resource?name=large") != 0,
          "View points the browser at the resource URL (data plane)");
    CHECK(strstr(outbuf, "<iframe") != 0,
          "View renders a native resource consumer (iframe)");
    CHECK(strstr(outbuf, "download='big.txt'") != 0,
          "View offers a download of the bulk resource");

    outbuf[0] = 0;
    CHECK(r0_s1_g1a_event("select-none", outbuf, (int)sizeof outbuf, &out_len) == 0,
          "browser View handles select-none");
    CHECK(strstr(outbuf, "/resource?name=large") == 0,
          "clearing removes the resource consumer");
}

static void phase_app_model(void) {
    printf("app model: effective authority + path authority\n");
    glon_app_t app;
    memset(&app, 0, sizeof app);
    snprintf(app.pkg_name, sizeof app.pkg_name, "%s", "Test App");
    app.npermissions = 0;
    snprintf(app.permissions[app.npermissions++], GLON_APP_NAME_MAX, "%s", "net/connect");
    snprintf(app.permissions[app.npermissions++], GLON_APP_NAME_MAX, "%s", "process/spawn");
    snprintf(app.permissions[app.npermissions++], GLON_APP_NAME_MAX, "%s", "bluetooth");

    char grants[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX];
    int ng = 0;
    snprintf(grants[ng++], GLON_APP_NAME_MAX, "%s", "net/connect");
    snprintf(grants[ng++], GLON_APP_NAME_MAX, "%s", "process/spawn");
    snprintf(grants[ng++], GLON_APP_NAME_MAX, "%s", "bluetooth");

    const char *const caps[] = { "net/connect", "file/app-write", "process/spawn", "view/open", NULL };
    char eff[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX];
    int ne = glon_app_effective(&app, grants, ng, caps, eff);
    int has_net = 0, has_spawn = 0, has_bt = 0;
    for (int i = 0; i < ne; i++) {
        if (!strcmp(eff[i], "net/connect")) has_net = 1;
        if (!strcmp(eff[i], "process/spawn")) has_spawn = 1;
        if (!strcmp(eff[i], "bluetooth")) has_bt = 1;
    }
    CHECK(has_net, "effective includes a requested+implemented+granted permission");
    CHECK(has_spawn, "effective includes a fully-granted permission");
    CHECK(!has_bt, "adding a permission the host does not implement is not granted");

    /* requested but not granted */
    char grants2[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX];
    int ng2 = 0;
    snprintf(grants2[ng2++], GLON_APP_NAME_MAX, "%s", "net/connect");
    int ne2 = glon_app_effective(&app, grants2, ng2, caps, eff);
    int spawn2 = 0;
    for (int i = 0; i < ne2; i++) if (!strcmp(eff[i], "process/spawn")) spawn2 = 1;
    CHECK(!spawn2, "requested but ungranted permission is not effective");

    /* unrequested */
    glon_app_t app3;
    memset(&app3, 0, sizeof app3);
    snprintf(app3.permissions[app3.npermissions++], GLON_APP_NAME_MAX, "%s", "net/connect");
    int ne3 = glon_app_effective(&app3, grants, ng, caps, eff);
    int spawn3 = 0;
    for (int i = 0; i < ne3; i++) if (!strcmp(eff[i], "process/spawn")) spawn3 = 1;
    CHECK(!spawn3, "permission not requested by the app is not effective");

    /* path authority */
    char out[512];
    CHECK(glon_app_resolve_write("/data", "ok.bin", out, sizeof out) == 0 &&
          strcmp(out, "/data/glon-fetch/downloads/ok.bin") == 0,
          "an authorised save name resolves inside the app-write area");
    CHECK(glon_app_resolve_write("/data", "../../etc/passwd", out, sizeof out) != 0,
          "a traversal save name is rejected");
    CHECK(glon_app_resolve_write("/data", "a/b", out, sizeof out) != 0,
          "a save name with a separator is rejected");
    CHECK(glon_app_resolve_write("/data", "/etc/passwd", out, sizeof out) != 0,
          "an absolute save name is rejected");
}

int main(void) {
    printf("Glon desktop application split (native, headless)\n");
    phase_native_app();
    phase_browser_view();
    phase_app_model();
    if (failures == 0) printf("DESKTOP_TEST PASS\n");
    else printf("DESKTOP_TEST FAIL (%d)\n", failures);
    return failures;
}
