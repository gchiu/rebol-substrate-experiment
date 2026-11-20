/* glon_win_compat.h - small MSVC compatibility shim for the optional HTTP
 * module.  It maps the POSIX primitives used by glon_http.c onto Win32.
 * This file is a host portability layer; it changes no Glon semantics. */
#ifndef GLON_WIN_COMPAT_H
#define GLON_WIN_COMPAT_H
#include <windows.h>
#include <signal.h>
#include <string.h>

typedef CRITICAL_SECTION pthread_mutex_t;
#define pthread_mutex_init(m, a) InitializeCriticalSection(m)
#define pthread_mutex_lock(m) EnterCriticalSection(m)
#define pthread_mutex_unlock(m) LeaveCriticalSection(m)
#define pthread_mutex_destroy(m) DeleteCriticalSection(m)
#define strcasecmp _stricmp
#ifndef usleep
#define usleep(us) Sleep((DWORD)((us) / 1000))
#endif
#endif /* GLON_WIN_COMPAT_H */
