#ifndef SCAN_UTIL_H
#define SCAN_UTIL_H

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "../third-party/utf8.h/utf8.h"
#include "macros.h"
#include <openssl/evp.h>

#define STR_STARTS_WITH_CONSTANT(x, y) ((x) != NULL && (y) != NULL && strncmp(y, x, sizeof(y) - 1) == 0)

#define TEXT_BUF_FULL (-1)
#define INITIAL_BUF_SIZE (1024 * 16)

#define SHOULD_IGNORE_CHAR(c) !(SHOULD_KEEP_CHAR(c))
#define SHOULD_KEEP_CHAR(c) (\
    ((c) >= '\'' && (c) <= ';') || \
    ((c) >= 'A' && (c) <= 'z') || \
    ((c) > 127 && (c) != 0x00A0 && (c) && (c) != 0xFFFD))


typedef struct dyn_buffer {
    char *buf;
    size_t cur;
    size_t size;
} dyn_buffer_t;

typedef struct text_buffer {
    long max_size;
    int last_char_was_whitespace;
    dyn_buffer_t dyn_buffer;
} text_buffer_t;

static int utf8_validchr2(const char *s) {
    if (0x00 == (0x80 & *s)) {
        return TRUE;
    } else if (0xf0 == (0xf8 & *s)) {
        if ((0x80 != (0xc0 & s[1])) || (0x80 != (0xc0 & s[2])) ||
            (0x80 != (0xc0 & s[3]))) {
            return FALSE;
        }

        if (0x80 == (0xc0 & s[4])) {
            return FALSE;
        }

        if ((0 == (0x07 & s[0])) && (0 == (0x30 & s[1]))) {
            return FALSE;
        }
    } else if (0xe0 == (0xf0 & *s)) {
        if ((0x80 != (0xc0 & s[1])) || (0x80 != (0xc0 & s[2]))) {
            return FALSE;
        }

        if (0x80 == (0xc0 & s[3])) {
            return FALSE;
        }

        if ((0 == (0x0f & s[0])) && (0 == (0x20 & s[1]))) {
            return FALSE;
        }
    } else if (0xc0 == (0xe0 & *s)) {
        if (0x80 != (0xc0 & s[1])) {
            return FALSE;
        }

        if (0x80 == (0xc0 & s[2])) {
            return FALSE;
        }

        if (0 == (0x1e & s[0])) {
            return FALSE;
        }
    } else {
        return FALSE;
    }

    return TRUE;
}


static dyn_buffer_t dyn_buffer_create() {
    dyn_buffer_t buf;

    buf.size = INITIAL_BUF_SIZE;
    buf.cur = 0;
    buf.buf = (char *) malloc(INITIAL_BUF_SIZE);

    return buf;
}

static void grow_buffer(dyn_buffer_t *buf, size_t size) {
    if (buf->cur + size > buf->size) {
        do {
            buf->size *= 2;
        } while (buf->cur + size > buf->size);

        buf->buf = (char *) realloc(buf->buf, buf->size);
    }
}

static void grow_buffer_small(dyn_buffer_t *buf) {
    if (buf->cur + sizeof(long) > buf->size) {
        buf->size *= 2;
        buf->buf = (char *) realloc(buf->buf, buf->size);
    }
}

static void dyn_buffer_write(dyn_buffer_t *buf, const void *data, size_t size) {
    grow_buffer(buf, size);

    memcpy(buf->buf + buf->cur, data, size);
    buf->cur += size;
}

static void dyn_buffer_write_char(dyn_buffer_t *buf, char c) {
    grow_buffer_small(buf);

    *(buf->buf + buf->cur) = c;
    buf->cur += sizeof(c);
}

static void dyn_buffer_write_str(dyn_buffer_t *buf, const char *str) {
    dyn_buffer_write(buf, str, strlen(str));
    dyn_buffer_write_char(buf, '\0');
}

static void dyn_buffer_append_string(dyn_buffer_t *buf, const char *str) {
    dyn_buffer_write(buf, str, strlen(str));
}

static void dyn_buffer_write_int(dyn_buffer_t *buf, int d) {
    grow_buffer_small(buf);

    *(int *) (buf->buf + buf->cur) = d;
    buf->cur += sizeof(int);
}

static void dyn_buffer_write_short(dyn_buffer_t *buf, uint16_t s) {
    grow_buffer_small(buf);

    *(uint16_t *) (buf->buf + buf->cur) = s;
    buf->cur += sizeof(uint16_t);
}

static void dyn_buffer_write_long(dyn_buffer_t *buf, unsigned long l) {
    grow_buffer_small(buf);

    *(unsigned long *) (buf->buf + buf->cur) = l;
    buf->cur += sizeof(unsigned long);
}

static void dyn_buffer_destroy(dyn_buffer_t *buf) {
    free(buf->buf);
}

static void text_buffer_destroy(text_buffer_t *buf) {
    dyn_buffer_destroy(&buf->dyn_buffer);
}

static text_buffer_t text_buffer_create(long max_size) {
    text_buffer_t text_buf;

    text_buf.dyn_buffer = dyn_buffer_create();
    text_buf.max_size = max_size;
    text_buf.last_char_was_whitespace = FALSE;

    return text_buf;
}

static int text_buffer_append_char(text_buffer_t *buf, int c) {

    if (SHOULD_IGNORE_CHAR(c) || c == ' ') {
        if (!buf->last_char_was_whitespace && buf->dyn_buffer.cur != 0) {
            dyn_buffer_write_char(&buf->dyn_buffer, ' ');
            buf->last_char_was_whitespace = TRUE;

            if (buf->max_size > 0 && buf->dyn_buffer.cur > buf->max_size) {
                return TEXT_BUF_FULL;
            }
        }
    } else {
        buf->last_char_was_whitespace = FALSE;
        grow_buffer_small(&buf->dyn_buffer);

        if (((utf8_int32_t) 0xffffff80 & c) == 0) {
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = (char) c;
        } else if (((utf8_int32_t) 0xfffff800 & c) == 0) {
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0xc0 | (char) (c >> 6);
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0x80 | (char) (c & 0x3f);
        } else if (((utf8_int32_t) 0xffff0000 & c) == 0) {
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0xe0 | (char) (c >> 12);
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0x80 | (char) ((c >> 6) & 0x3f);
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0x80 | (char) (c & 0x3f);
        } else {
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0xf0 | (char) (c >> 18);
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0x80 | (char) ((c >> 12) & 0x3f);
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0x80 | (char) ((c >> 6) & 0x3f);
            *(buf->dyn_buffer.buf + buf->dyn_buffer.cur++) = 0x80 | (char) (c & 0x3f);
        }

        if (buf->max_size > 0 && buf->dyn_buffer.cur > buf->max_size) {
            return TEXT_BUF_FULL;
        }
    }

    return 0;
}


static void text_buffer_terminate_string(text_buffer_t *buf) {
    if (buf->dyn_buffer.cur > 0 && *(buf->dyn_buffer.buf + buf->dyn_buffer.cur - 1) == ' ') {
        *(buf->dyn_buffer.buf + buf->dyn_buffer.cur - 1) = '\0';
    } else {
        dyn_buffer_write_char(&buf->dyn_buffer, '\0');
    }
}

// Naive UTF16 -> ascii conversion
static int text_buffer_append_string16_le(text_buffer_t *buf, const char *str, size_t len) {
    int ret = 0;
    for (int i = 1; i < len; i += 2) {
        ret = text_buffer_append_char(buf, str[i]);
    }
    return ret;
}

static int text_buffer_append_string16_be(text_buffer_t *buf, const char *str, size_t len) {
    int ret = 0;
    for (int i = 0; i < len; i += 2) {
        ret = text_buffer_append_char(buf, str[i]);
    }
    return ret;
}

#define UTF8_END_OF_STRING \
    (ptr - str >= len || *ptr == 0 || \
    (0xc0 == (0xe0 & *ptr) && ptr - str > len - 2) || \
    (0xe0 == (0xf0 & *ptr) && ptr - str > len - 3) || \
    (0xf0 == (0xf8 & *ptr) && ptr - str > len - 4))

static int text_buffer_append_string(text_buffer_t *buf, const char *str, size_t len) {

    const char *ptr = str;
    const char *oldPtr = ptr;

    if (str == NULL || UTF8_END_OF_STRING) {
        return 0;
    }

    if (len <= 4) {
        for (int i = 0; i < len; i++) {
            if (((utf8_int32_t) 0xffffff80 & str[i]) == 0 && SHOULD_KEEP_CHAR(str[i])) {
                dyn_buffer_write_char(&buf->dyn_buffer, str[i]);
            }
        }
        return 0;
    }

    utf8_int32_t c;
    char tmp[16] = {0};

    do {
        ptr = (char *) utf8codepoint(ptr, &c);
        *(int *) tmp = 0x00000000;
        memcpy(tmp, oldPtr, ptr - oldPtr);
        oldPtr = ptr;

        if (!utf8_validchr2(tmp)) {
            continue;
        }

        int ret = text_buffer_append_char(buf, c);

        if (ret != 0) {
            return ret;
        }
    } while (!UTF8_END_OF_STRING);

    return 0;
}

static int text_buffer_append_string0(text_buffer_t *buf, const char *str) {
    return text_buffer_append_string(buf, str, strlen(str));
}

static int text_buffer_append_markup(text_buffer_t *buf, const char *markup) {

    int tag_open = TRUE;
    const char *ptr = markup;
    const char *start = markup;

    while (*ptr != '\0') {
        if (tag_open) {
            if (*ptr == '>') {
                tag_open = FALSE;
                start = ptr + 1;
            }
        } else {
            if (*ptr == '<') {
                tag_open = TRUE;
                if (ptr != start) {
                    if (text_buffer_append_string(buf, start, (ptr - start)) == TEXT_BUF_FULL) {
                        return TEXT_BUF_FULL;
                    }
                    if (text_buffer_append_char(buf, ' ') == TEXT_BUF_FULL) {
                        return TEXT_BUF_FULL;
                    }
                }
            }
        }

        ptr += 1;
    }

    if (ptr != start) {
        if (text_buffer_append_string(buf, start, (ptr - start)) == TEXT_BUF_FULL) {
            return TEXT_BUF_FULL;
        }
        if (text_buffer_append_char(buf, ' ') == TEXT_BUF_FULL) {
            return TEXT_BUF_FULL;
        }
    }
    return 0;
}

static void *read_all(vfile_t *f, size_t *size) {
    void *buf = malloc(f->st_size);
    *size = f->read(f, buf, f->st_size);

    if (*size != f->st_size) {
        free(buf);
        return NULL;
    }

    return buf;
}

#define STACK_BUFFER_SIZE (size_t)(4096 * 8)

__always_inline
static void safe_digest_update(EVP_MD_CTX *ctx, void *buf, size_t size) {
    unsigned char stack_buf[STACK_BUFFER_SIZE];

    void *sha1_buf;
    if (size <= STACK_BUFFER_SIZE) {
        sha1_buf = stack_buf;
    } else {
        void *heap_sha1_buf = malloc(size);
        sha1_buf = heap_sha1_buf;
    }

    memcpy(sha1_buf, buf, size);
    EVP_DigestUpdate(ctx, sha1_buf, size);

    if (sha1_buf != stack_buf) {
        free(sha1_buf);
    }
}

static parse_job_t *create_parse_job(const char *filepath, int mtime, size_t st_size) {
    parse_job_t *job = (parse_job_t *) malloc(sizeof(parse_job_t));

    job->parent[0] = '\0';

    strcpy(job->filepath, filepath);
    strcpy(job->vfile.filepath, filepath);
    job->vfile.st_size = st_size;
    job->vfile.mtime = mtime;

    const char *slash = strrchr(filepath, '/');
    if (slash == NULL) {
        job->base = 0;
    } else {
        job->base = (int) (slash - filepath + 1);
    }

    const char *dot = strrchr(filepath + job->base, '.');
    if (dot == NULL) {
        job->ext = (int) strlen(filepath);
    } else {
        job->ext = (int) (dot - filepath + 1);
    }

    job->vfile.fd = -1;
    job->vfile.is_fs_file = TRUE;
    job->vfile.has_checksum = FALSE;
    job->vfile.rewind_buffer_size = 0;
    job->vfile.rewind_buffer = NULL;

    return job;
}


static int meta_contains_key (meta_line_t *meta_head, enum metakey key) {

    meta_line_t *meta = meta_head;
    while (meta != NULL) {
        if (meta->key == key) {
            return TRUE;
        }
        meta = meta->next;
    }

    return FALSE;
}

#endif
