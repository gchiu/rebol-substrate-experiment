#ifndef GLON_PLATFORM_WIN_H
#define GLON_PLATFORM_WIN_H
#ifdef _WIN32
#include <windows.h>
#include <time.h>
#include <stdlib.h>
#include <io.h>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 2
#endif
static int glon_win_clock_gettime(int clk, struct timespec *ts) {
    if (clk == CLOCK_MONOTONIC) {
        static LARGE_INTEGER freq;
        static int have_freq = 0;
        LARGE_INTEGER now;
        if (!have_freq) { QueryPerformanceFrequency(&freq); have_freq = 1; }
        QueryPerformanceCounter(&now);
        long long ns = (long long)((now.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
        ts->tv_sec = (time_t)(now.QuadPart / freq.QuadPart);
        ts->tv_nsec = (long)ns;
        return 0;
    }
    if (clk == CLOCK_PROCESS_CPUTIME_ID) {
        FILETIME create, exit, kernel, user;
        if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) return -1;
        ULARGE_INTEGER k, u;
        k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
        unsigned long long t = k.QuadPart + u.QuadPart;
        ts->tv_sec = (time_t)(t / 10000000ULL);
        ts->tv_nsec = (long)((t % 10000000ULL) * 100ULL);
        return 0;
    }
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    unsigned long long t = u.QuadPart - 116444736000000000ULL;
    ts->tv_sec = (time_t)(t / 10000000ULL);
    ts->tv_nsec = (long)((t % 10000000ULL) * 100ULL);
    return 0;
}
#define clock_gettime glon_win_clock_gettime
static int glon_win_setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value);
}
#define setenv glon_win_setenv
#endif
#endif
