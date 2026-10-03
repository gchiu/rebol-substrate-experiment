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

int main(void) {
    printf("Glon desktop application split (native, headless)\n");
    phase_native_app();
    phase_browser_view();
    if (failures == 0) printf("DESKTOP_TEST PASS\n");
    else printf("DESKTOP_TEST FAIL (%d)\n", failures);
    return failures;
}
