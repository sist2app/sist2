#ifndef SIST2_WORKER_PROTOCOL_H
#define SIST2_WORKER_PROTOCOL_H

#include "src/sist.h"

#include <stdint.h>

/**
 * Framing for the master <-> worker pipes.
 *
 * A frame is an 8 byte header ({u32 type, u32 payload length}) followed by the payload. Fixed-width
 * fields are stored in native byte order: both ends are the same executable running on the same
 * machine, so there is nothing to convert.
 *
 * Only these exchanges exist:
 *   master -> worker   JOB, BYE, RSP_MARK
 *   worker -> master   DOC, THUMB, REQ_MARK, DONE, CURRENT_JOB
 *
 * The master sends at most one JOB per worker at a time and always drains the worker's output, so a
 * worker blocking on a large write can never deadlock against a master blocking on a write. REQ_MARK
 * is the one exception and is strictly request/response.
 */

#define SIST_PATH_MAX (PATH_MAX * 2 + 1)

#define FRAME_HEADER_SIZE (8)
#define FRAME_MAX_PAYLOAD (256 * 1024 * 1024)

typedef enum {
    FRAME_JOB = 1,
    FRAME_BYE = 2,
    FRAME_RSP_MARK = 3,
    FRAME_DOC = 4,
    FRAME_THUMB = 5,
    FRAME_REQ_MARK = 6,
    FRAME_DONE = 7,
    /** Payload is a path, no length prefix: the archive member currently being parsed */
    FRAME_CURRENT_JOB = 8,
} frame_type_t;

typedef struct {
    uint32_t type;
    uint32_t len;
    char *payload;
} frame_t;

typedef struct {
    char path[SIST_PATH_MAX];
    int mtime;
    int64_t size;
} proto_job_t;

typedef struct {
    char path[SIST_PATH_MAX];
    char parent[SIST_PATH_MAX];
    unsigned int mime;
    int mtime;
    int64_t size;
    int thumbnail_count;
    /** NULL for the placeholder row an archive is inserted with before its children */
    char *json;
} proto_doc_t;

typedef struct {
    int index;
    const void *data;
    size_t size;
} proto_thumb_t;

typedef struct {
    char path[SIST_PATH_MAX];
    int mtime;
} proto_mark_t;

/* Blocking IO, used by the worker side */

/**
 * @return 0 on success, -1 on error or on a payload over FRAME_MAX_PAYLOAD.
 */
int frame_write(int fd, uint32_t type, const void *payload, uint32_t len);

/**
 * @return 0 on success, 1 on clean EOF, -1 on error or protocol violation.
 */
int frame_read(int fd, frame_t *frame);

void frame_free(frame_t *frame);

/* Incremental decoding, used by the master side because libuv hands over arbitrary chunks */

struct frame_parser;
typedef struct frame_parser frame_parser_t;

/**
 * The frame (payload included) is only valid for the duration of the call.
 */
typedef void (*frame_cb_t)(const frame_t *frame, void *user_data);

frame_parser_t *frame_parser_create();

void frame_parser_destroy(frame_parser_t *parser);

/**
 * @return 0 on success, -1 on protocol violation.
 */
int frame_parser_feed(frame_parser_t *parser, const char *data, size_t len, frame_cb_t cb, void *user_data);

/* Payload encoding. Encoders return a malloc'd buffer, decoders return 0 on success and -1 on a
 * malformed payload. */

char *proto_encode_job(const proto_job_t *job, uint32_t *len);

int proto_decode_job(const frame_t *frame, proto_job_t *job);

char *proto_encode_doc(const proto_doc_t *doc, uint32_t *len);

/**
 * doc->json is malloc'd and owned by the caller.
 */
int proto_decode_doc(const frame_t *frame, proto_doc_t *doc);

char *proto_encode_thumb(int index, const void *data, size_t size, uint32_t *len);

/**
 * thumb->data points into the frame payload.
 */
int proto_decode_thumb(const frame_t *frame, proto_thumb_t *thumb);

char *proto_encode_mark(const proto_mark_t *mark, uint32_t *len);

int proto_decode_mark(const frame_t *frame, proto_mark_t *mark);

char *proto_encode_i32(int32_t value, uint32_t *len);

int proto_decode_i32(const frame_t *frame, int32_t *value);

#endif
