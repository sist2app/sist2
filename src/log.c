#include "log.h"
#include "ctx.h"

#include <pthread.h>
#include <stdarg.h>

const char *log_colors[] = {
        "\033[34m", "\033[01;34m", "\033[01;33m", "\033[0m", "\033[31m", "\033[01;31m"
};

const char *log_levels[] = {
        "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"
};

void log_write(int fd, const char *buf, size_t len) {
    while (len > 0) {
        const ssize_t written = write(fd, buf, len);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        buf += written;
        len -= written;
    }
}

void vsist_logf(const char *filepath, int level, char *format, va_list ap) {

    static int is_tty = -1;
    if (is_tty == -1) {
        is_tty = isatty(STDERR_FILENO);
    }

    char log_str[LOG_MAX_LENGTH];

    char datetime[32];
    time_t t;
    struct tm result;
    t = time(NULL);
    localtime_r(&t, &result);
    strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", &result);

    int log_len;
    if (LogCtx.json_logs) {
        vsnprintf(log_str, sizeof(log_str), format, ap);

        cJSON *log_str_json = cJSON_CreateString(log_str);
        char *log_str_json_str = cJSON_PrintUnformatted(log_str_json);

        cJSON *filepath_json = cJSON_CreateString(filepath);
        char *filepath_json_str = cJSON_PrintUnformatted(filepath_json);

        log_len = snprintf(
                log_str, sizeof(log_str),
                "{\"thread\":\"T%d\",\"datetime\":\"%s\",\"level\":\"%s\",\"filepath\":%s,\"message\":%s}\n",
                ProcData.thread_id, datetime, log_levels[level], filepath_json_str, log_str_json_str
        );

        cJSON_Delete(filepath_json);
        cJSON_Delete(log_str_json);
        free(log_str_json_str);
        free(filepath_json_str);

        log_write(STDOUT_FILENO, log_str, log_len);
        return;
    }

    if (is_tty) {
        log_len = snprintf(
                log_str, sizeof(log_str),
                "\033[%dmT%d%s [%s] [%s %s] ",
                31 + ProcData.thread_id % 7, ProcData.thread_id, log_colors[level],
                datetime, log_levels[level], filepath
        );
    } else {
        log_len = snprintf(
                log_str, sizeof(log_str),
                "T%d [%s] [%s %s] ",
                ProcData.thread_id, datetime, log_levels[level], filepath
        );
    }

    size_t maxsize = sizeof(log_str) - log_len;
    log_len += vsnprintf(log_str + log_len, maxsize, format, ap);

    if ((size_t) log_len >= maxsize) {
        fprintf(stderr, "([%s] FIXME: Log string is too long to display: %dB)\n",
                log_levels[level], log_len);
        return;
    }

    if (is_tty) {
        log_len += sprintf(log_str + log_len, "\033[0m\n");
    } else {
        *(log_str + log_len) = '\n';
        log_len += 1;
    }

    if (PrintingProgressBar) {
        PrintingProgressBar = FALSE;
        memmove(log_str + 1, log_str, log_len);
        log_str[0] = '\n';
        log_len += 1;
    }

    log_write(STDERR_FILENO, log_str, log_len);
}

void sist_logf(const char *filepath, int level, char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsist_logf(filepath, level, format, ap);
    va_end(ap);
}

void sist_log(const char *filepath, int level, char *str) {

    static int is_tty = -1;
    if (is_tty == -1) {
        is_tty = isatty(STDERR_FILENO);
    }

    char log_str[LOG_MAX_LENGTH];

    char datetime[32];
    time_t t;
    struct tm result;
    t = time(NULL);
    localtime_r(&t, &result);
    strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", &result);

    int log_len;

    if (LogCtx.json_logs) {
        cJSON *log_str_json = cJSON_CreateString(str);
        char *log_str_json_str = cJSON_PrintUnformatted(log_str_json);

        cJSON *filepath_json = cJSON_CreateString(filepath);
        char *filepath_json_str = cJSON_PrintUnformatted(filepath_json);

        log_len = snprintf(
                log_str, sizeof(log_str),
                "{\"thread\":\"T%d\",\"datetime\":\"%s\",\"level\":\"%s\",\"filepath\":%s,\"message\":%s}\n",
                ProcData.thread_id, datetime, log_levels[level], filepath_json_str, log_str_json_str
        );

        cJSON_Delete(log_str_json);
        cJSON_Delete(filepath_json);
        free(log_str_json_str);
        free(filepath_json_str);

        log_write(STDOUT_FILENO, log_str, log_len);
        return;
    }
    if (is_tty) {
        log_len = snprintf(
                log_str, sizeof(log_str),
                "\033[%dmT%d%s [%s] [%s %s] %s \033[0m\n",
                31 + ProcData.thread_id % 7, ProcData.thread_id, log_colors[level],
                datetime, log_levels[level], filepath,
                str
        );
    } else {
        log_len = snprintf(
                log_str, sizeof(log_str),
                "T%d [%s] [%s %s] %s \n",
                ProcData.thread_id, datetime, log_levels[level], filepath,
                str
        );
    }

    if (PrintingProgressBar) {
        PrintingProgressBar = FALSE;
        memmove(log_str + 1, log_str, log_len);
        log_str[0] = '\n';
        log_len += 1;
    }

    log_write(STDERR_FILENO, log_str, log_len);
}
