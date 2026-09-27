/* r0_s1_g1a_live_tests.c -- focused native test for the experimental
 * GLON_LIVE host boundary and the text Live Translate application logic.
 *
 * Build with -DGLON_LIVE and link r0_s1_g1a_live.c; this is not part of the
 * frozen `s1` test binary.
 *
 * It loads the real demo/shop/live.glon application (with common.glon and
 * strings.glon) and proves:
 *   - Start emits host_call(listen-start) with no language and no HTML;
 *   - the capture console renders status/error but never source or translated
 *     text;
 *   - Stop emits host_call(listen-stop);
 *   - a >200-byte UTF-8 reply still arrives through glon_event_bytes.
 */

#include "r0_s1.h"
#include "r0_s1_g1a_live.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static char filebuf[65536];
static char wrapbuf[65536];
static char outbuf[65536];

/* load one Glon source file persistently, stripping `;;` comments.
 * A source that is not already one outer [ ... ] block (common.glon) is wrapped
 * so that the block-context `masm`/`raw` keywords parse as keywords. */
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "live-test: cannot open %s\n", path); return -1; }
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
        wrapbuf[0] = '[';
        wrapbuf[1] = ' ';
        memcpy(wrapbuf + 2, filebuf, len);
        wrapbuf[len + 2] = ' ';
        wrapbuf[len + 3] = ']';
        wrapbuf[len + 4] = 0;
        src = wrapbuf;
    }

    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "live-test: parse error in %s\n", path); return -2; }
    r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "live-test: run failed in %s\n", path); return -3; }
    return 0;
}

static char got_op[256];
static char got_arg[256];
static int  got_calls = 0;

static void record_host_call(const char *op, int op_len,
                             const char *arg, int arg_len, void *user) {
    (void)user;
    if (op_len > (int)sizeof got_op - 1) op_len = (int)sizeof got_op - 1;
    if (arg_len > (int)sizeof got_arg - 1) arg_len = (int)sizeof got_arg - 1;
    memcpy(got_op, op, (size_t)op_len); got_op[op_len] = 0;
    memcpy(got_arg, arg, (size_t)arg_len); got_arg[arg_len] = 0;
    got_calls++;
}

static int dispatch_bytes(const char *token, const char *text, char *out, int cap) {
    int out_len = 0;
    int rc = r0_s1_g1a_live_event_bytes(token, (const unsigned char *)text,
                                        (unsigned int)strlen(text), out, cap, &out_len);
    if (rc != 0) return -1;
    if (out_len < cap) out[out_len] = 0;
    return out_len;
}

int main(void) {
    printf("R0-S1 G1A live text application (GLON_LIVE native)\n");

    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    CHECK(load_file("demo/shop/common.glon") == 0, "environment: common.glon loads");
    CHECK(load_file("demo/shop/strings.glon") == 0, "environment: strings.glon loads");
    CHECK(load_file("demo/shop/live.glon") == 0, "application: live.glon loads");
    if (failures) return failures;

    r0_s1_g1a_live_set_host_call(record_host_call, 0);

    /* Start emits the relay connect intent, no HTML and no language. */
    int out_len = 0;
    int rc = r0_s1_g1a_live_event("start", outbuf, (int)sizeof outbuf, &out_len);
    CHECK(rc == 0 && out_len == 0, "outbound: Start yields a host-call, no HTML");
    CHECK(got_calls == 1 && strcmp(got_op, "listen-start") == 0 && got_arg[0] == 0,
          "outbound: host_call received listen-start with no language");

    /* Status event renders. */
    CHECK(dispatch_bytes("qwen-status", "Listening", outbuf, (int)sizeof outbuf) > 0
          && strstr(outbuf, "Listening") != 0, "event: qwen-status renders Listening");

    /* The capture console never shows source or translated text. */
    CHECK(dispatch_bytes("qwen-source-delta", "I'm speaking", outbuf, (int)sizeof outbuf) > 0
          && strstr(outbuf, "I'm speaking") == 0, "event: source text is not displayed");
    CHECK(dispatch_bytes("qwen-text-delta", "我在说", outbuf, (int)sizeof outbuf) > 0
          && strstr(outbuf, "我在说") == 0, "event: translation text is not displayed");

    /* Error event renders. */
    CHECK(dispatch_bytes("qwen-error", "401 InvalidApiKey", outbuf, (int)sizeof outbuf) > 0
          && strstr(outbuf, "401 InvalidApiKey") != 0, "event: error rendered");

    /* Stop emits the semantic stop intent, no HTML. */
    out_len = 0;
    rc = r0_s1_g1a_live_event("stop", outbuf, (int)sizeof outbuf, &out_len);
    CHECK(rc == 0 && out_len == 0, "outbound: Stop yields a host-call, no HTML");
    CHECK(got_calls == 2 && strcmp(got_op, "listen-stop") == 0 && got_arg[0] == 0,
          "outbound: host_call received listen-stop");

    /* The raw >200-byte inbound path is still exercised (through the error line). */
    static char longbuf[16384];
    int len = 0;
    const char *base =
        "这是一个用于验证 Glon 主机调用和原始字节事件往返的中文测试字符串。"
        "它包含中文、日本語、한국어、emoji 😀🎤🌐 以及 ASCII 边界。";
    for (int i = 0; i < 6; i++) {
        size_t n = strlen(base);
        if (len + (int)n >= (int)sizeof longbuf) break;
        memcpy(longbuf + len, base, n);
        len += (int)n;
    }
    longbuf[len] = 0;
    CHECK(len > 200, "inbound: payload exceeds the old 200-byte value cap");
    CHECK(dispatch_bytes("qwen-error", longbuf, outbuf, (int)sizeof outbuf) > 0
          && strstr(outbuf, longbuf) != 0,
          "inbound: glon_event_bytes carries the full >200-byte UTF-8 payload");

    if (failures == 0) printf("all GLON_LIVE text-application tests passed\n");
    return failures;
}
