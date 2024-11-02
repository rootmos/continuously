// libr 0.5.0 (46bc9a44ec3019926e9b043a14e939d3e0a886fb) (https://github.com/rootmos/libr.git) (2024-11-02T08:38:28+01:00)
// modules: fail logging now devnull nonblock

#ifndef LIBR_HEADER
#define LIBR_HEADER

#define LIBR(x) x
#define PRIVATE __attribute__((visibility("hidden")))
#define PUBLIC __attribute__((visibility("default")))
#define API PRIVATE


// libr: fail.h

#define CHECK(res, format, ...) CHECK_NOT(res, -1, format, ##__VA_ARGS__)

#define CHECK_NOT(res, err, format, ...) \
    CHECK_IF(res == err, format, ##__VA_ARGS__)

#define CHECK_IF(cond, format, ...) do { \
    if(cond) { \
        LIBR(failwith0)(__extension__ __FUNCTION__, __extension__ __FILE__, \
            __extension__ __LINE__, 1, \
            format "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define CHECK_MALLOC(x) CHECK_NOT(x, NULL, "memory allocation failed")
#define CHECK_MMAP(x) CHECK_NOT(x, MAP_FAILED, "memory mapping failed")

#define failwith(format, ...) \
    LIBR(failwith0)(__extension__ __FUNCTION__, __extension__ __FILE__, \
        __extension__ __LINE__, 0, format "\n", ##__VA_ARGS__)

#define not_implemented() do { failwith("not implemented"); } while(0)

void LIBR(failwith0)(
    const char* const caller,
    const char* const file,
    const unsigned int line,
    const int include_errno,
    const char* const fmt, ...)
__attribute__ ((noreturn, format (printf, 5, 6)));

// libr: logging.h

#include <stdarg.h>

#define LOG_QUIET 0
#define LOG_ERROR 1
#define LOG_WARNING 2
#define LOG_INFO 3
#define LOG_DEBUG 4
#define LOG_TRACE 5

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
#endif

extern int LIBR(logger_fd);

#define __r_log(level, format, ...) do { \
    LIBR(logger)(level, __extension__ __FUNCTION__, __extension__ __FILE__, \
          __extension__ __LINE__, format "\n", ##__VA_ARGS__); \
} while(0)

#ifdef __cplusplus
void LIBR(dummy)(...);
#else
void LIBR(dummy)();
#endif

#if LOG_LEVEL >= LOG_ERROR
#define error(format, ...) __r_log(LOG_ERROR, format, ##__VA_ARGS__)
#else
#define error(format, ...) do { if(0) LIBR(dummy)(__VA_ARGS__); } while(0)
#endif

#if LOG_LEVEL >= LOG_WARNING
#define warning(format, ...) __r_log(LOG_WARNING, format, ##__VA_ARGS__)
#else
#define warning(format, ...) do { if(0) LIBR(dummy)(__VA_ARGS__); } while(0)
#endif

#if LOG_LEVEL >= LOG_INFO
#define info(format, ...) __r_log(LOG_INFO, format, ##__VA_ARGS__)
#else
#define info(format, ...) do { if(0) LIBR(dummy)(__VA_ARGS__); } while(0)
#endif

#if LOG_LEVEL >= LOG_DEBUG
#define debug(format, ...) __r_log(LOG_DEBUG, format, ##__VA_ARGS__)
#else
#define debug(format, ...) do { if(0) LIBR(dummy)(__VA_ARGS__); } while(0)
#endif

#if LOG_LEVEL >= LOG_TRACE
#define trace(format, ...) __r_log(LOG_TRACE, format, ##__VA_ARGS__)
#else
#define trace(format, ...) do { if(0) LIBR(dummy)(__VA_ARGS__); } while(0)
#endif

void LIBR(logger)(
    int level,
    const char* const caller,
    const char* const file,
    const unsigned int line,
    const char* const fmt, ...)
__attribute__ ((format (printf, 5, 6)));

void LIBR(vlogger)(
    int level,
    const char* const caller,
    const char* const file,
    const unsigned int line,
    const char* const fmt,
    va_list vl
);

// libr: now.h

// returns current time formated as compact ISO8601: 20190123T182628Z
const char* LIBR(now_iso8601_compact)(void);

// libr: devnull.h

int LIBR(devnull)(int flags);
void LIBR(devnull2)(int fd, int flags);

// libr: nonblock.h

void LIBR(set_blocking)(int fd, int blocking);
#endif // LIBR_HEADER

#ifdef LIBR_IMPLEMENTATION

// libr: fail.c

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

API void LIBR(failwith0)(
    const char* const caller,
    const char* const file,
    const unsigned int line,
    const int include_errno,
    const char* const fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);

    if(include_errno) {
        LIBR(logger)(LOG_ERROR, caller, file, line, "(%s) ", strerror(errno));
        if(vdprintf(LIBR(logger_fd), fmt, vl) < 0) {
            abort();
        }
    } else {
        LIBR(vlogger)(LOG_ERROR, caller, file, line, fmt, vl);
    }
    va_end(vl);

    abort();
}

// libr: logging.c

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#ifdef __cplusplus
API void LIBR(dummy)(...)
#else
API void LIBR(dummy)()
#endif
{
    abort();
}

int LIBR(logger_fd) API = 2;

API void LIBR(vlogger)(
    int level,
    const char* const caller,
    const char* const file,
    const unsigned int line,
    const char* const fmt, va_list vl)
{
    int r = dprintf(LIBR(logger_fd), "%s:%d:%s:%s:%u ",
        LIBR(now_iso8601_compact)(), getpid(), caller, file, line);
    if(r < 0) {
        abort();
    }

    r = vdprintf(LIBR(logger_fd), fmt, vl);
    if(r < 0) {
        abort();
    }
}

API void LIBR(logger)(
    int level,
    const char* const caller,
    const char* const file,
    const unsigned int line,
    const char* const fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    LIBR(vlogger)(level, caller, file, line, fmt, vl);
    va_end(vl);
}

// libr: now.c

#include <time.h>
#include <stdlib.h>

PRIVATE const char* LIBR(now_iso8601_compact)(void)
{
    static char buf[17];
    const time_t t = time(NULL);
    size_t r = strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", gmtime(&t));
    if(r <= 0) abort();
    return buf;
}

// libr: devnull.c

#include <fcntl.h>
#include <unistd.h>

API int LIBR(devnull)(int flags)
{
    int fd = open("/dev/null", flags);
    CHECK(fd, "open(/dev/null, %d)", flags);
    return fd;
}

API void LIBR(devnull2)(int fd, int flags)
{
    int null = LIBR(devnull)(flags);
    int r = dup2(null, fd); CHECK(r, "dup2(.., %d)", fd);
    r = close(null); CHECK(r, "close");
}

// libr: nonblock.c

#include <fcntl.h>

API void LIBR(set_blocking)(int fd, int blocking)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if(blocking) {
        fl &= ~O_NONBLOCK;
    } else {
        fl |= O_NONBLOCK;
    }

    int r = fcntl(fd, F_SETFL, fl);
    CHECK(r, "fcntl(%d, F_SETFL, %d)", fd, fl);
}
#endif // LIBR_IMPLEMENTATION
