#ifndef SCAN_JSON_H
#define SCAN_JSON_H

#include "../scan.h"


typedef struct {
    long content_size;
    log_callback_t log;
    logf_callback_t logf;
    unsigned int json_mime;
    unsigned int ndjson_mime;
} scan_json_ctx_t;

scan_code_t parse_json(scan_json_ctx_t *ctx, vfile_t *f, document_t *doc);

scan_code_t parse_ndjson(scan_json_ctx_t *ctx, vfile_t *f, document_t *doc);

__always_inline
static int is_json(scan_json_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->json_mime;
}

__always_inline
static int is_ndjson(scan_json_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->ndjson_mime;
}

#endif
