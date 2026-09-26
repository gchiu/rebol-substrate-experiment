/* r0_s1_g1a_live_tests.c -- focused native test for the experimental
 * GLON_LIVE host boundary.  Build with -DGLON_LIVE and link r0_s1_g1a_live.c;
 * this is not part of the frozen `s1` test binary.
 *
 * It proves the round trip end to end in C:
 *   - Glon renders a host-call request fragment (the real demo/shop/live.glon
 *     app source, loaded with common.glon + strings.glon);
 *   - the registered host callback receives op/arg ("echo"/"roundtrip");
 *   - the callback's asynchronous reply is delivered back through
 *     r0_s1_g1a_live_event_bytes() with > 200 bytes of UTF-8 including Chinese;
 *   - Glon renders that reply into its page fragment.
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

static const char *BASE =
    "这是一个用于验证 Glon 主机调用和原始字节事件往返的中文测试字符串。"
    "它包含中文、日本語、한국어、emoji 😀🎤🌐 以及 ASCII 边界。";

int main(void) {
    printf("R0-S1 G1A live host boundary (GLON_LIVE native)\n");

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

    /* outbound: Glon requests the host capability */
    int out_len = 0;
    int rc = r0_s1_g1a_live_event("go", outbuf, (int)sizeof outbuf, &out_len);
    CHECK(rc == 0 && out_len == 0, "outbound: host-call request dispatch yields no HTML");
    CHECK(got_calls == 1 && strcmp(got_op, "echo") == 0 && strcmp(got_arg, "roundtrip") == 0,
          "outbound: callback received op=echo arg=roundtrip");

    /* inbound: asynchronous reply > 200 bytes with non-ASCII UTF-8 */
    static char longbuf[16384];
    int len = 0;
    for (int i = 0; i < 6; i++) {
        size_t n = strlen(BASE);
        if (len + (int)n >= (int)sizeof longbuf) break;
        memcpy(longbuf + len, BASE, n);
        len += (int)n;
    }
    longbuf[len] = 0;
    CHECK(len > 200, "inbound: test payload exceeds the old 200-byte value cap");

    out_len = 0;
    rc = r0_s1_g1a_live_event_bytes("reply", (const unsigned char *)longbuf,
                                    (unsigned int)len, outbuf, (int)sizeof outbuf, &out_len);
    CHECK(rc == 0 && out_len > 0, "inbound: reply dispatch renders HTML");
    if (out_len > 0 && out_len < (int)sizeof outbuf) outbuf[out_len] = 0;
    CHECK(out_len > 0 && strstr(outbuf, longbuf) != 0,
          "inbound: rendered page contains the full >200-byte UTF-8 reply");
    CHECK(out_len > 0 && strstr(outbuf, "中文测试字符串") != 0,
          "inbound: rendered page contains the Chinese text");

    if (failures == 0) printf("all GLON_LIVE host-boundary tests passed\n");
    return failures;
}
