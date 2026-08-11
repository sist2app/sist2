#ifndef SCAN_SCAN_MSDOC_H
#define SCAN_SCAN_MSDOC_H

#include "../scan.h"

typedef struct {
    long content_size;
    log_callback_t log;
    logf_callback_t logf;
    unsigned int msdoc_mime;
} scan_msdoc_ctx_t;

__always_inline
static int is_msdoc(scan_msdoc_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->msdoc_mime;
}

void parse_msdoc(scan_msdoc_ctx_t *ctx, vfile_t *f, document_t *doc);

void parse_msdoc_text(scan_msdoc_ctx_t *ctx, document_t *doc, FILE *file_in, void* buf, size_t buf_len);

#endif
