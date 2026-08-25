#ifndef SIST2_LOG_H
#define SIST2_LOG_H


#include <signal.h>
#include <stddef.h>

// Raised before a fatal exit so a debugger stops with the stack still up. Windows has no SIGUSR1.
#ifdef _WIN32
#define RAISE_FATAL() ((void) 0)
#else
#define RAISE_FATAL() raise(SIGUSR1)
#endif

#define LOG_MAX_LENGTH 8192

/** Best-effort write of a log line; retries partial writes, drops the line on error */
void log_write(int fd, const char *buf, size_t len);

#define LOG_SIST_DEBUG 0
#define LOG_SIST_INFO 1
#define LOG_SIST_WARNING 2
#define LOG_SIST_ERROR 3
#define LOG_SIST_FATAL 4

#define LOG_DEBUGF(filepath, fmt, ...) do{\
    if (LogCtx.very_verbose) {sist_logf(filepath, LOG_SIST_DEBUG, fmt, __VA_ARGS__);}}while(0)
#define LOG_DEBUG(filepath, str) do{\
    if (LogCtx.very_verbose) {sist_log(filepath, LOG_SIST_DEBUG, str);}}while(0)

#define LOG_INFOF(filepath, fmt, ...) do {\
    if (LogCtx.verbose) {sist_logf(filepath, LOG_SIST_INFO, fmt, __VA_ARGS__);}} while(0)
#define LOG_INFO(filepath, str) do {\
    if (LogCtx.verbose) {sist_log(filepath, LOG_SIST_INFO, str);}} while(0)

#define LOG_WARNINGF(filepath, fmt, ...) do {\
    if (LogCtx.verbose) {sist_logf(filepath, LOG_SIST_WARNING, fmt, __VA_ARGS__);}}while(0)
#define LOG_WARNING(filepath, str) do{\
    if (LogCtx.verbose) {sist_log(filepath, LOG_SIST_WARNING, str);}}while(0)

#define LOG_ERRORF(filepath, fmt, ...) do {\
    if (LogCtx.verbose) {sist_logf(filepath, LOG_SIST_ERROR, fmt, __VA_ARGS__);}}while(0)
#define LOG_ERROR(filepath, str) do{\
    if (LogCtx.verbose) {sist_log(filepath, LOG_SIST_ERROR, str);}}while(0)

#define LOG_FATALF(filepath, fmt, ...)\
    sist_logf(filepath, LOG_SIST_FATAL, fmt, __VA_ARGS__);\
    RAISE_FATAL();                    \
    exit(-1)
#define LOG_FATAL(filepath, str) \
    sist_log(filepath, LOG_SIST_FATAL, str);\
    RAISE_FATAL();                    \
    exit(-1)
#define LOG_FATALF_NO_EXIT(filepath, fmt, ...) \
    sist_logf(filepath, LOG_SIST_FATAL, fmt, __VA_ARGS__)
#define LOG_FATAL_NO_EXIT(filepath, str) \
    sist_log(filepath, LOG_SIST_FATAL, str)

#include "sist.h"

/* printf attribute: a mismatched format argument in a log line is a crash, not a wrong message */
void sist_logf(const char *filepath, int level, char *format, ...) __attribute__((format(SIST_PRINTF_FORMAT, 3, 4)));

void vsist_logf(const char *filepath, int level, char *format, va_list ap);

void sist_log(const char *filepath, int level, char *str);

#endif
