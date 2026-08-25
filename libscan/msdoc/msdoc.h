#ifndef SCAN_SCAN_MSDOC_H
#define SCAN_SCAN_MSDOC_H

#include "../scan.h"

typedef struct {
    int64_t content_size;
    log_callback_t log;
    logf_callback_t logf;
    unsigned int msdoc_mime;
} scan_msdoc_ctx_t;

__always_inline
static int is_msdoc(scan_msdoc_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->msdoc_mime;
}

void parse_msdoc(scan_msdoc_ctx_t *ctx, vfile_t *f, document_t *doc);

/* takes ownership of buf */
void parse_msdoc_buf(scan_msdoc_ctx_t *ctx, document_t *doc, const char *filepath,
                     void *buf, size_t buf_len);

#endif
