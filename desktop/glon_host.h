/* desktop/glon_host.h -- the tiny OS boundary for the desktop proof.
 *
 * Only the primitive capabilities the proof needs: a TCP byte stream, a
 * process/open, and process-lifecycle glue.  The filesystem read is portable
 * C stdio and lives in glon_desktop.c.  Each platform supplies one
 * implementation; no other file contains OS-specific code.
 *
 * No application semantics live here: the host never sees "greeting" or any
 * logical name, only byte buffers and paths chosen by Glon.
 */
#ifndef GLON_HOST_H
#define GLON_HOST_H

#include <stdint.h>

typedef intptr_t glon_socket;
#define GLON_INVALID_SOCKET ((glon_socket)-1)

int  glon_host_init(void);
void glon_host_shutdown(void);
void glon_ignore_sigpipe(void);
long glon_process_id(void);

glon_socket glon_tcp_listen(const char *addr, int port, int *bound_port);
glon_socket glon_tcp_accept(glon_socket srv);
int  glon_tcp_read(glon_socket c, char *buf, int cap);
int  glon_tcp_write_all(glon_socket c, const void *buf, int n);
void glon_tcp_close(glon_socket c);

void glon_open_browser(const char *url);
void glon_open_path(const char *path);

/* Create a directory and any missing parents (best effort). */
int glon_mkdirs(const char *path);

/* Generic process/spawn.  Runs `exe` with a separate argv array (NULL
 * terminated) -- never a shell.  stdout+stderr are captured and delivered as
 * records (split on LF or CR) to cb as they arrive.  Returns 0 and sets
 * *exit_code when the child finished. */
int glon_spawn_stream(const char *exe, char *const argv[],
                      void (*cb)(const char *record, int len, void *user),
                      void *user, int *exit_code);

/* SHA-256 of a file using the platform facility (sha256sum / certutil).
 * Writes 64 lowercase hex chars + NUL to out_hex.  A service, not authority. */
int glon_sha256(const char *path, char *out_hex, int cap);

#endif /* GLON_HOST_H */
