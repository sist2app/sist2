#include "util.h"
#include "src/ctx.h"

#include <wordexp.h>

#define PBSTR "========================================"
#define PBWIDTH 40

dyn_buffer_t url_escape(char *str) {

    dyn_buffer_t text = dyn_buffer_create();

    char *ptr = str;
    while (*ptr) {
        if (*ptr == '#') {
            dyn_buffer_write(&text, "%23", 3);
            ptr++;
        }

        dyn_buffer_write_char(&text, *ptr++);
    }
    dyn_buffer_write_char(&text, '\0');

    return text;
}

char *abspath(const char *path) {
    char *expanded = expandpath(path);

    char *abs = realpath(expanded, NULL);
    free(expanded);
    if (abs == NULL) {
        return NULL;
    }
    if (strlen(abs) > 1) {
        abs = realloc(abs, strlen(abs) + 1);
    }

    return abs;
}

void shell_escape(char *dst, const char *src) {
    const char *ptr = src;
    char *out = dst;
    while ((*ptr)) {
        char c = *ptr++;

        if (c == '&' || c == '\n' || c == '|' || c == ';' || c == '<' ||
            c == '>' || c == '(' || c == ')' || c == '{' || c == '}') {
            *out++ = '\\';
        }
        *out++ = c;
    }
    *out = 0;
}

char *expandpath(const char *path) {
    char tmp[PATH_MAX * 2];

    shell_escape(tmp, path);

    wordexp_t w;
    wordexp(tmp, &w, 0);

    if (w.we_wordv == NULL) {
        return NULL;
    }

    *tmp = '\0';
    for (size_t i = 0; i < w.we_wordc; i++) {
        strcat(tmp, w.we_wordv[i]);
        if (i != w.we_wordc - 1) {
            strcat(tmp, " ");
        }
    }

    char *expanded = malloc(strlen(tmp) + 1);
    strcpy(expanded, tmp);

    wordfree(&w);
    return expanded;
}

int PrintingProgressBar = 0;

#define BOOLEAN_STRING(x) ((x) == 0  ? "false" : "true")

void progress_bar_print_json(size_t done, size_t count, size_t tn_size, size_t index_size, int waiting) {

    char log_str[1024];

    size_t log_len = snprintf(
            log_str, sizeof(log_str),
            "{\"progress\": {\"done\":%lu,\"count\":%lu,\"tn_size\":%lu,\"index_size\":%lu,\"waiting\":%s}}\n",
            done, count, tn_size, index_size, BOOLEAN_STRING(waiting)
    );

    write(STDOUT_FILENO, log_str, log_len);
}

void progress_bar_print(double percentage, size_t tn_size, size_t index_size) {

    if (isnan(percentage)) {
        return;
    }

    // TODO: Fix this with shm/ctx
    static int last_val = -1;

    int val = (int) (percentage * 100);
    if (last_val == val || val > 100) {
        return;
    }
    last_val = val;

    int lpad = (int) ((percentage + 0.01) * PBWIDTH);
    int rpad = PBWIDTH - lpad;

    char tn_unit;
    if (tn_size > 1000 * 1000 * 1000) {
        tn_size = tn_size / 1000 / 1000 / 1000;
        tn_unit = 'G';
    } else {
        tn_size = tn_size / 1000 / 1000;
        tn_unit = 'M';
    }

    char index_unit;
    if (index_size > 1000 * 1000 * 1000) {
        index_size = index_size / 1000 / 1000 / 1000;
        index_unit = 'G';
    } else {
        index_size = index_size / 1000 / 1000;
        index_unit = 'M';
    }

    if (tn_size == 0 && index_size == 0) {
        fprintf(stderr,
                "\r%3d%%[%.*s>%*s]",
                val, lpad, PBSTR, rpad, ""
        );
    } else {
        fprintf(stderr,
                "\r%3d%%[%.*s>%*s] TN:%3d%c IDX:%3d%c",
                val, lpad, PBSTR, rpad, "",
                (int) tn_size, tn_unit,
                (int) index_size, index_unit
        );
    }

    PrintingProgressBar = TRUE;
}


const char *find_file_in_paths(const char *paths[], const char *filename) {

    for (int i = 0; paths[i] != NULL; i++) {

        char *apath = abspath(paths[i]);
        if (apath == NULL) {
            continue;
        }

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", apath, filename);

        LOG_DEBUGF("util.c", "Looking for '%s' in folder '%s' (%s)", filename, apath, path);
        free(apath);

        struct stat info;
        int ret = stat(path, &info);
        if (ret != -1) {
            return paths[i];
        }
    }

    return NULL;
}

#define ESCAPE_CHAR ']'

/** Longest escape sequence one input byte can produce: `]XX` */
#define ESCAPE_MAX_EXPANSION (3)

/** utf8codepoint() reads the whole sequence a lead byte announces, so the copy is padded */
#define ESCAPE_PADDING (8)

size_t str_escape_size(const char *str) {
    return strlen(str) * ESCAPE_MAX_EXPANSION + 1;
}

void str_escape(char *dst, size_t dst_size, const char *str) {
    if (dst_size == 0) {
        return;
    }

    const size_t len = strlen(str);

    char stack_buf[4096];
    char *buf = (len + ESCAPE_PADDING <= sizeof(stack_buf))
                ? stack_buf
                : malloc(len + ESCAPE_PADDING);

    memcpy(buf, str, len);
    memset(buf + len, 0, ESCAPE_PADDING);

    char *cur = dst;
    // The last byte is reserved for the terminator, and a sequence that does not fit whole is dropped
    const char *const end = dst + dst_size - 1;

    const char *ptr = buf;
    const char *oldPtr = ptr;

    utf8_int32_t c;
    char tmp[16];

    do {
        ptr = (char *) utf8codepoint(ptr, &c);
        *(int *) tmp = 0x00000000;
        size_t code_len = (ptr - oldPtr);
        memcpy(tmp, oldPtr, code_len);
        oldPtr = ptr;

        if (!utf8_validchr2(tmp)) {
            for (size_t i = 0; i < code_len; i++) {
                if (tmp[i] == 0) {
                    break;
                }

                if (end - cur < 3) {
                    goto truncated;
                }

                cur += sprintf(cur, "%c%02X", ESCAPE_CHAR, (unsigned char) tmp[i]);
            }
            continue;
        }

        if (c == ESCAPE_CHAR) {
            if (end - cur < 2) {
                goto truncated;
            }
            *cur++ = ESCAPE_CHAR;
            *cur++ = ESCAPE_CHAR;
            continue;
        }

        if (((utf8_int32_t) 0xffffff80 & c) == 0) {
            if (end - cur < 1) {
                goto truncated;
            }
            *(cur++) = (char) c;
        } else if (((utf8_int32_t) 0xfffff800 & c) == 0) {
            if (end - cur < 2) {
                goto truncated;
            }
            *(cur++) = 0xc0 | (char) (c >> 6);
            *(cur++) = 0x80 | (char) (c & 0x3f);
        } else if (((utf8_int32_t) 0xffff0000 & c) == 0) {
            if (end - cur < 3) {
                goto truncated;
            }
            *(cur++) = 0xe0 | (char) (c >> 12);
            *(cur++) = 0x80 | (char) ((c >> 6) & 0x3f);
            *(cur++) = 0x80 | (char) (c & 0x3f);
        } else {
            if (end - cur < 4) {
                goto truncated;
            }
            *(cur++) = 0xf0 | (char) (c >> 18);
            *(cur++) = 0x80 | (char) ((c >> 12) & 0x3f);
            *(cur++) = 0x80 | (char) ((c >> 6) & 0x3f);
            *(cur++) = 0x80 | (char) (c & 0x3f);
        }

    } while (*ptr != '\0');

truncated:
    *cur = '\0';

    if (buf != stack_buf) {
        free(buf);
    }
}

void str_unescape(char *dst, const char *str) {
    char *cur = dst;
    const char *ptr = str;

    char tmp[3];
    tmp[2] = '\0';

    while (*ptr != 0) {
        char c = *ptr++;

        if (c == ESCAPE_CHAR) {
            char next = *ptr;

            if (next == ESCAPE_CHAR) {
                *cur++ = (char) c;
                ptr += 1;
            } else {
                tmp[0] = *(ptr);
                tmp[1] = *(ptr + 1);
                *cur++ = (char) strtol(tmp, NULL, 16);
                ptr += 2;
            }
        } else {
            *cur++ = c;
        }
    }
    *cur = '\0';
}

#define NSEC_PER_SEC 1000000000

struct timespec timespec_normalise(struct timespec ts) {
    while (ts.tv_nsec >= NSEC_PER_SEC) {
        ts.tv_sec += 1;
        ts.tv_nsec -= NSEC_PER_SEC;
    }

    while (ts.tv_nsec <= -NSEC_PER_SEC) {
        ts.tv_sec -= 1;
        ts.tv_nsec += NSEC_PER_SEC;
    }

    if (ts.tv_nsec < 0) {
        ts.tv_sec -= 1;
        ts.tv_nsec = (NSEC_PER_SEC + ts.tv_nsec);
    }

    return ts;
}

struct timespec timespec_add(struct timespec ts1, long usec) {
    ts1 = timespec_normalise(ts1);

    struct timespec ts2 = timespec_normalise((struct timespec) {
            .tv_sec = 0,
            .tv_nsec = usec * 1000
    });

    ts1.tv_sec += ts2.tv_sec;
    ts1.tv_nsec += ts2.tv_nsec;

    return timespec_normalise(ts1);
}

