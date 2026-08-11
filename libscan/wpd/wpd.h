#ifndef SIST2_WPD_H
#define SIST2_WPD_H

#include "../scan.h"
#include "../util.h"

typedef struct {
    long content_size;

    log_callback_t log;
    logf_callback_t logf;

    unsigned int wpd_mime;
} scan_wpd_ctx_t;

scan_code_t parse_wpd(scan_wpd_ctx_t *ctx, vfile_t *f, document_t *doc);

__always_inline
static int is_wpd(scan_wpd_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->wpd_mime;
}

#endif
