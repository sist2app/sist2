#include "protocol.h"

/* Cursor helpers. The writer counts first, then fills a buffer of the size it counted; the reader
 * refuses to step past the end of the payload. */

typedef struct {
    char *buf;
    size_t cursor;
    size_t capacity;
} buf_writer_t;

typedef struct {
    const char *buf;
    size_t cursor;
    size_t len;
    int failed;
} buf_reader_t;

static void write_bytes(buf_writer_t *writer, const void *data, size_t size) {
    memcpy(writer->buf + writer->cursor, data, size);
    writer->cursor += size;
}

#define WRITE_SCALAR(writer, value) do {         \
    __typeof__(value) _tmp = (value);            \
    write_bytes((writer), &_tmp, sizeof(_tmp));  \
} while (0)

static int read_bytes(buf_reader_t *reader, void *out, size_t size) {
    if (reader->failed || reader->cursor + size > reader->len) {
        reader->failed = TRUE;
        return FALSE;
    }

    memcpy(out, reader->buf + reader->cursor, size);
    reader->cursor += size;

    return TRUE;
}

static const char *read_slice(buf_reader_t *reader, size_t size) {
    if (reader->failed || reader->cursor + size > reader->len) {
        reader->failed = TRUE;
        return NULL;
    }

    const char *slice = reader->buf + reader->cursor;
    reader->cursor += size;

    return slice;
}

// Copies a length-prefixed string into a fixed size buffer, NUL-terminating it
static int read_string(buf_reader_t *reader, char *out, size_t capacity) {
    uint32_t size;
    if (!read_bytes(reader, &size, sizeof(size))) {
        return FALSE;
    }

    if (size >= capacity) {
        reader->failed = TRUE;
        return FALSE;
    }

    const char *slice = read_slice(reader, size);
    if (slice == NULL) {
        return FALSE;
    }

    memcpy(out, slice, size);
    out[size] = '\0';

    return TRUE;
}

static void write_string(buf_writer_t *writer, const char *str) {
    uint32_t size = (uint32_t) strlen(str);

    WRITE_SCALAR(writer, size);
    write_bytes(writer, str, size);
}

static size_t string_size(const char *str) {
    return sizeof(uint32_t) + strlen(str);
}

static char *alloc_payload(buf_writer_t *writer, size_t size, uint32_t *len) {
    writer->buf = malloc(size);
    writer->cursor = 0;
    writer->capacity = size;
    *len = (uint32_t) size;

    return writer->buf;
}

/* Blocking IO */

static int fd_write_all(int fd, const void *data, size_t size) {
    const char *cursor = data;

    while (size > 0) {
        ssize_t written = write(fd, cursor, size);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        cursor += written;
        size -= written;
    }

    return 0;
}

/**
 * @return 0 on success, 1 if nothing at all was read, -1 on error or truncated read.
 */
static int fd_read_all(int fd, void *data, size_t size) {
    char *cursor = data;
    size_t remaining = size;

    while (remaining > 0) {
        ssize_t got = read(fd, cursor, remaining);

        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (got == 0) {
            return remaining == size ? 1 : -1;
        }

        cursor += got;
        remaining -= got;
    }

    return 0;
}

int frame_write(int fd, uint32_t type, const void *payload, uint32_t len) {
    uint32_t header[2] = {type, len};

    if (fd_write_all(fd, header, sizeof(header)) != 0) {
        return -1;
    }

    if (len > 0 && fd_write_all(fd, payload, len) != 0) {
        return -1;
    }

    return 0;
}

int frame_read(int fd, frame_t *frame) {
    uint32_t header[2];

    int ret = fd_read_all(fd, header, sizeof(header));
    if (ret != 0) {
        return ret;
    }

    if (header[1] > FRAME_MAX_PAYLOAD) {
        return -1;
    }

    frame->type = header[0];
    frame->len = header[1];
    frame->payload = NULL;

    if (frame->len > 0) {
        frame->payload = malloc(frame->len);

        if (fd_read_all(fd, frame->payload, frame->len) != 0) {
            free(frame->payload);
            frame->payload = NULL;
            return -1;
        }
    }

    return 0;
}

void frame_free(frame_t *frame) {
    free(frame->payload);
    frame->payload = NULL;
}

/* Incremental decoding */

typedef struct frame_parser {
    char *buf;
    size_t size;
    size_t capacity;
} frame_parser_t;

frame_parser_t *frame_parser_create() {
    frame_parser_t *parser = malloc(sizeof(frame_parser_t));

    parser->capacity = 8192;
    parser->buf = malloc(parser->capacity);
    parser->size = 0;

    return parser;
}

void frame_parser_destroy(frame_parser_t *parser) {
    free(parser->buf);
    free(parser);
}

int frame_parser_feed(frame_parser_t *parser, const char *data, size_t len, frame_cb_t cb, void *user_data) {
    if (parser->size + len > parser->capacity) {
        while (parser->capacity < parser->size + len) {
            parser->capacity *= 2;
        }
        parser->buf = realloc(parser->buf, parser->capacity);
    }

    memcpy(parser->buf + parser->size, data, len);
    parser->size += len;

    size_t consumed = 0;
    while (parser->size - consumed >= FRAME_HEADER_SIZE) {
        uint32_t header[2];
        memcpy(header, parser->buf + consumed, sizeof(header));

        if (header[1] > FRAME_MAX_PAYLOAD) {
            return -1;
        }

        size_t frame_size = FRAME_HEADER_SIZE + header[1];
        if (parser->size - consumed < frame_size) {
            break;
        }

        frame_t frame = {
                .type = header[0],
                .len = header[1],
                .payload = header[1] == 0 ? NULL : parser->buf + consumed + FRAME_HEADER_SIZE
        };
        cb(&frame, user_data);

        consumed += frame_size;
    }

    if (consumed > 0) {
        memmove(parser->buf, parser->buf + consumed, parser->size - consumed);
        parser->size -= consumed;
    }

    return 0;
}

/* Payload encoding */

char *proto_encode_job(const proto_job_t *job, uint32_t *len) {
    buf_writer_t writer;
    alloc_payload(&writer, sizeof(int32_t) + sizeof(int64_t) + string_size(job->path), len);

    WRITE_SCALAR(&writer, (int32_t) job->mtime);
    WRITE_SCALAR(&writer, (int64_t) job->size);
    write_string(&writer, job->path);

    return writer.buf;
}

int proto_decode_job(const frame_t *frame, proto_job_t *job) {
    buf_reader_t reader = {.buf = frame->payload, .len = frame->len};

    int32_t mtime;
    int64_t size;

    read_bytes(&reader, &mtime, sizeof(mtime));
    read_bytes(&reader, &size, sizeof(size));
    read_string(&reader, job->path, sizeof(job->path));

    if (reader.failed) {
        return -1;
    }

    job->mtime = mtime;
    job->size = size;

    return 0;
}

char *proto_encode_doc(const proto_doc_t *doc, uint32_t *len) {
    int32_t json_len = doc->json == NULL ? -1 : (int32_t) strlen(doc->json);

    buf_writer_t writer;
    alloc_payload(
            &writer,
            sizeof(uint32_t) + sizeof(int32_t) + sizeof(int64_t) + sizeof(int32_t)
            + string_size(doc->path) + string_size(doc->parent)
            + sizeof(int32_t) + (json_len < 0 ? 0 : (size_t) json_len),
            len
    );

    WRITE_SCALAR(&writer, (uint32_t) doc->mime);
    WRITE_SCALAR(&writer, (int32_t) doc->mtime);
    WRITE_SCALAR(&writer, (int64_t) doc->size);
    WRITE_SCALAR(&writer, (int32_t) doc->thumbnail_count);
    write_string(&writer, doc->path);
    write_string(&writer, doc->parent);
    WRITE_SCALAR(&writer, json_len);
    if (json_len > 0) {
        write_bytes(&writer, doc->json, json_len);
    }

    return writer.buf;
}

int proto_decode_doc(const frame_t *frame, proto_doc_t *doc) {
    buf_reader_t reader = {.buf = frame->payload, .len = frame->len};

    uint32_t mime = 0;
    int32_t mtime = 0;
    int64_t size = 0;
    int32_t thumbnail_count = 0;
    int32_t json_len = 0;

    read_bytes(&reader, &mime, sizeof(mime));
    read_bytes(&reader, &mtime, sizeof(mtime));
    read_bytes(&reader, &size, sizeof(size));
    read_bytes(&reader, &thumbnail_count, sizeof(thumbnail_count));
    read_string(&reader, doc->path, sizeof(doc->path));
    read_string(&reader, doc->parent, sizeof(doc->parent));
    read_bytes(&reader, &json_len, sizeof(json_len));

    if (reader.failed || json_len < -1) {
        return -1;
    }

    doc->json = NULL;
    if (json_len >= 0) {
        const char *slice = read_slice(&reader, json_len);
        if (slice == NULL) {
            return -1;
        }

        doc->json = malloc(json_len + 1);
        memcpy(doc->json, slice, json_len);
        doc->json[json_len] = '\0';
    }

    doc->mime = mime;
    doc->mtime = mtime;
    doc->size = size;
    doc->thumbnail_count = thumbnail_count;

    return 0;
}

char *proto_encode_thumb(int index, const void *data, size_t size, uint32_t *len) {
    buf_writer_t writer;
    alloc_payload(&writer, sizeof(int32_t) + size, len);

    WRITE_SCALAR(&writer, (int32_t) index);
    write_bytes(&writer, data, size);

    return writer.buf;
}

int proto_decode_thumb(const frame_t *frame, proto_thumb_t *thumb) {
    buf_reader_t reader = {.buf = frame->payload, .len = frame->len};

    int32_t index;
    if (!read_bytes(&reader, &index, sizeof(index))) {
        return -1;
    }

    thumb->index = index;
    thumb->size = reader.len - reader.cursor;
    thumb->data = thumb->size == 0 ? NULL : frame->payload + reader.cursor;

    return 0;
}

char *proto_encode_mark(const proto_mark_t *mark, uint32_t *len) {
    buf_writer_t writer;
    alloc_payload(&writer, sizeof(int32_t) + string_size(mark->path), len);

    WRITE_SCALAR(&writer, (int32_t) mark->mtime);
    write_string(&writer, mark->path);

    return writer.buf;
}

int proto_decode_mark(const frame_t *frame, proto_mark_t *mark) {
    buf_reader_t reader = {.buf = frame->payload, .len = frame->len};

    int32_t mtime;

    read_bytes(&reader, &mtime, sizeof(mtime));
    read_string(&reader, mark->path, sizeof(mark->path));

    if (reader.failed) {
        return -1;
    }

    mark->mtime = mtime;

    return 0;
}

char *proto_encode_i32(int32_t value, uint32_t *len) {
    buf_writer_t writer;
    alloc_payload(&writer, sizeof(int32_t), len);

    WRITE_SCALAR(&writer, value);

    return writer.buf;
}

int proto_decode_i32(const frame_t *frame, int32_t *value) {
    buf_reader_t reader = {.buf = frame->payload, .len = frame->len};

    if (!read_bytes(&reader, value, sizeof(*value))) {
        return -1;
    }

    return 0;
}
