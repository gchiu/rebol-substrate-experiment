/* glon_io_file_cli.h - asynchronous host driver for glon-io-file. */
#ifndef GLON_IO_FILE_CLI_H
#define GLON_IO_FILE_CLI_H

#include "glon_io_file.h"

typedef struct glon_io_cli glon_io_cli;

int glon_io_cli_init(glon_io_cli **out, const glon_io_policy *policy,
                     char *err, size_t errcap);
void glon_io_cli_destroy(glon_io_cli *cli);

/* Start one request on a worker thread.  At most one may be outstanding. */
int glon_io_cli_submit(glon_io_cli *cli, const uint8_t *request, size_t len,
                       char *err, size_t errcap);

/* Wait for the worker (after the caller has run any independent job) and
 * return the encoded reply.  Returns 0 on a usable reply, -1 on protocol
 * failure or cancellation. */
int glon_io_cli_wait(glon_io_cli *cli, uint8_t *reply, size_t cap,
                     size_t *reply_len, char *err, size_t errcap);

/* Cooperative cancellation: ask the worker to discard its result.  A syscall
 * already in progress is not interrupted; wait/join completes it. */
void glon_io_cli_cancel(glon_io_cli *cli);

unsigned glon_io_cli_cleanup(glon_io_cli *cli, unsigned *failed_close);
unsigned glon_io_cli_open_handles(const glon_io_cli *cli);

#endif /* GLON_IO_FILE_CLI_H */
