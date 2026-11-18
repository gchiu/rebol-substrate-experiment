/* glon_io_file_cli.c - one-outstanding-request asynchronous host driver.
 *
 * The main Glon thread never performs filesystem calls for a request.  It
 * submits the frame to a worker thread, may run another host-dispatched Glon
 * job, and only then waits for completion.  This is host-level concurrency; it
 * is not M1 task suspension and does not add a blocked task state.
 */
#define _POSIX_C_SOURCE 200809L
#include "glon_io_file_cli.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct glon_io_cli {
    glon_io_service service;
    pthread_t worker;
    int running;
    atomic_int done;
    atomic_int cancel;
    unsigned char request[GLON_IO_MAX_FRAME];
    size_t request_len;
    unsigned char reply[GLON_IO_MAX_FRAME];
    size_t reply_len;
    char worker_err[256];
    unsigned delay_ms;
};

static void sleep_ms(unsigned ms) {
    if (ms == 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000u;
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { /* resume */ }
}

static void *worker_main(void *arg) {
    glon_io_cli *cli = arg;
    sleep_ms(cli->delay_ms);
    if (atomic_load_explicit(&cli->cancel, memory_order_acquire)) {
        atomic_store_explicit(&cli->done, 1, memory_order_release);
        return NULL;
    }

    glon_io_request req;
    glon_io_reply rep;
    char err[256] = {0};
    if (glon_io_decode_request(cli->request, cli->request_len, &req,
                               err, sizeof err) != 0) {
        snprintf(cli->worker_err, sizeof cli->worker_err,
                 "request decode: %.200s", err);
        atomic_store_explicit(&cli->done, 1, memory_order_release);
        return NULL;
    }
    (void)glon_io_execute(&cli->service, &req, &rep, err, sizeof err);
    if (glon_io_encode_reply(&rep, cli->reply, sizeof cli->reply,
                             &cli->reply_len, err, sizeof err) != 0) {
        snprintf(cli->worker_err, sizeof cli->worker_err,
                 "reply encode: %.200s", err);
        cli->reply_len = 0;
    }
    atomic_store_explicit(&cli->done, 1, memory_order_release);
    return NULL;
}

int glon_io_cli_init(glon_io_cli **out, const glon_io_policy *policy,
                     char *err, size_t errcap) {
    glon_io_cli *cli = calloc(1, sizeof *cli);
    if (!cli) {
        if (errcap) snprintf(err, errcap, "out of memory");
        return -1;
    }
    if (glon_io_service_init(&cli->service, policy, err, errcap) != 0) {
        free(cli);
        return -1;
    }
    cli->delay_ms = policy->delay_ms;
    atomic_init(&cli->done, 0);
    atomic_init(&cli->cancel, 0);
    *out = cli;
    return 0;
}

void glon_io_cli_destroy(glon_io_cli *cli) {
    if (!cli) return;
    if (cli->running) {
        atomic_store(&cli->cancel, 1);
        (void)pthread_join(cli->worker, NULL);
        cli->running = 0;
    }
    (void)glon_io_service_cleanup(&cli->service, NULL);
    free(cli);
}

int glon_io_cli_submit(glon_io_cli *cli, const uint8_t *request, size_t len,
                       char *err, size_t errcap) {
    if (cli->running) {
        if (errcap) snprintf(err, errcap, "one request already outstanding");
        return GLON_IO_BUSY;
    }
    if (len == 0 || len > sizeof cli->request) {
        if (errcap) snprintf(err, errcap, "request frame out of bounds");
        return GLON_IO_TOO_LARGE;
    }
    memcpy(cli->request, request, len);
    cli->request_len = len;
    cli->reply_len = 0;
    cli->worker_err[0] = 0;
    atomic_store(&cli->done, 0);
    atomic_store(&cli->cancel, 0);
    if (pthread_create(&cli->worker, NULL, worker_main, cli) != 0) {
        if (errcap) snprintf(err, errcap, "cannot start I/O worker");
        return GLON_IO_IO_ERROR;
    }
    cli->running = 1;
    return GLON_IO_OK;
}

int glon_io_cli_wait(glon_io_cli *cli, uint8_t *reply, size_t cap,
                     size_t *reply_len, char *err, size_t errcap) {
    *reply_len = 0;
    if (!cli->running) {
        if (errcap) snprintf(err, errcap, "no outstanding request");
        return -1;
    }
    while (!atomic_load_explicit(&cli->done, memory_order_acquire))
        sleep_ms(1);
    (void)pthread_join(cli->worker, NULL);
    cli->running = 0;
    if (atomic_load(&cli->cancel)) {
        if (errcap) snprintf(err, errcap, "request cancelled");
        return -1;
    }
    if (cli->worker_err[0]) {
        if (errcap) snprintf(err, errcap, "%s", cli->worker_err);
        return -1;
    }
    if (cli->reply_len == 0 || cli->reply_len > cap) {
        if (errcap) snprintf(err, errcap, "reply frame out of bounds");
        return -1;
    }
    memcpy(reply, cli->reply, cli->reply_len);
    *reply_len = cli->reply_len;
    return 0;
}

void glon_io_cli_cancel(glon_io_cli *cli) {
    atomic_store_explicit(&cli->cancel, 1, memory_order_release);
}

unsigned glon_io_cli_cleanup(glon_io_cli *cli, unsigned *failed_close) {
    return glon_io_service_cleanup(&cli->service, failed_close);
}

unsigned glon_io_cli_open_handles(const glon_io_cli *cli) {
    return glon_io_service_open_handles(&cli->service);
}
