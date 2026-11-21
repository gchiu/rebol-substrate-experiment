/* glon_io_file_cli_windows.c - Win32 one-request-at-a-time worker driver.
 *
 * The worker thread is created once per session.  Creating a thread for every
 * 400-byte file chunk dominated Windows streaming time, so requests are handed
 * to a persistent worker through an auto-reset event.  The contract is the
 * same as glon_io_file_cli.c: one outstanding request, cooperative cancel.
 */
#include <windows.h>
#include "glon_io_file_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct glon_io_cli {
    glon_io_service service;
    HANDLE worker;
    HANDLE request_event;
    HANDLE done_event;
    volatile LONG running;
    volatile LONG done;
    volatile LONG cancel;
    volatile LONG stop;
    unsigned char request[GLON_IO_MAX_FRAME];
    size_t request_len;
    unsigned char reply[GLON_IO_MAX_FRAME];
    size_t reply_len;
    char worker_err[256];
    unsigned delay_ms;
};

static DWORD WINAPI worker_main(LPVOID arg) {
    glon_io_cli *cli = (glon_io_cli *)arg;
    for (;;) {
        WaitForSingleObject(cli->request_event, INFINITE);
        if (InterlockedCompareExchange(&cli->stop, 0, 0)) break;
        if (cli->delay_ms) Sleep(cli->delay_ms);
        if (InterlockedCompareExchange(&cli->cancel, 0, 0)) {
            InterlockedExchange(&cli->done, 1);
            SetEvent(cli->done_event);
            continue;
        }
        glon_io_request req;
        glon_io_reply rep;
        char err[256] = {0};
        if (glon_io_decode_request(cli->request, cli->request_len, &req,
                                   err, sizeof err) != 0) {
            snprintf(cli->worker_err, sizeof cli->worker_err,
                     "request decode: %.200s", err);
        } else {
            (void)glon_io_execute(&cli->service, &req, &rep, err, sizeof err);
            if (glon_io_encode_reply(&rep, cli->reply, sizeof cli->reply,
                                     &cli->reply_len, err, sizeof err) != 0) {
                snprintf(cli->worker_err, sizeof cli->worker_err,
                         "reply encode: %.200s", err);
                cli->reply_len = 0;
            }
        }
        InterlockedExchange(&cli->done, 1);
        SetEvent(cli->done_event);
    }
    return 0;
}

int glon_io_cli_init(glon_io_cli **out, const glon_io_policy *policy,
                     char *err, size_t errcap) {
    glon_io_cli *cli = (glon_io_cli *)calloc(1, sizeof *cli);
    if (!cli) {
        if (errcap) snprintf(err, errcap, "out of memory");
        return -1;
    }
    if (glon_io_service_init(&cli->service, policy, err, errcap) != 0) {
        free(cli);
        return -1;
    }
    cli->delay_ms = policy->delay_ms;
    cli->request_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    cli->done_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!cli->request_event || !cli->done_event) {
        if (errcap) snprintf(err, errcap, "cannot create I/O events (win32 %lu)",
                             (unsigned long)GetLastError());
        if (cli->request_event) CloseHandle(cli->request_event);
        if (cli->done_event) CloseHandle(cli->done_event);
        glon_io_service_cleanup(&cli->service, NULL);
        free(cli);
        return -1;
    }
    cli->worker = CreateThread(NULL, 0, worker_main, cli, 0, NULL);
    if (!cli->worker) {
        if (errcap) snprintf(err, errcap, "cannot start I/O worker (win32 %lu)",
                             (unsigned long)GetLastError());
        CloseHandle(cli->request_event);
        CloseHandle(cli->done_event);
        glon_io_service_cleanup(&cli->service, NULL);
        free(cli);
        return -1;
    }
    *out = cli;
    return 0;
}

void glon_io_cli_destroy(glon_io_cli *cli) {
    if (!cli) return;
    if (cli->worker) {
        InterlockedExchange(&cli->stop, 1);
        SetEvent(cli->request_event);
        WaitForSingleObject(cli->worker, INFINITE);
        CloseHandle(cli->worker);
        cli->worker = NULL;
    }
    (void)glon_io_service_cleanup(&cli->service, NULL);
    if (cli->request_event) CloseHandle(cli->request_event);
    if (cli->done_event) CloseHandle(cli->done_event);
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
    InterlockedExchange(&cli->done, 0);
    InterlockedExchange(&cli->cancel, 0);
    cli->running = 1;
    SetEvent(cli->request_event);
    return GLON_IO_OK;
}

int glon_io_cli_wait(glon_io_cli *cli, uint8_t *reply, size_t cap,
                     size_t *reply_len, char *err, size_t errcap) {
    *reply_len = 0;
    if (!cli->running) {
        if (errcap) snprintf(err, errcap, "no outstanding request");
        return -1;
    }
    WaitForSingleObject(cli->done_event, INFINITE);
    cli->running = 0;
    if (InterlockedCompareExchange(&cli->cancel, 0, 0)) {
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
    InterlockedExchange(&cli->cancel, 1);
}

unsigned glon_io_cli_cleanup(glon_io_cli *cli, unsigned *failed_close) {
    return glon_io_service_cleanup(&cli->service, failed_close);
}

unsigned glon_io_cli_open_handles(const glon_io_cli *cli) {
    return glon_io_service_open_handles(&cli->service);
}
