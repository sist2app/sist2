#ifndef SCAN_CBR_H
#define SCAN_CBR_H

#include <stdlib.h>
#include "../ebook/ebook.h"

typedef struct {
    log_callback_t log;
    logf_callback_t logf;

    int enable_tn;
    int tn_size;
    int tn_qscale;

    unsigned int cbr_mime;
    unsigned int cbz_mime;
} scan_comic_ctx_t;

__always_inline
static int is_cbr(scan_comic_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->cbr_mime;
}

__always_inline
static int is_cbz(scan_comic_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->cbz_mime;
}

void parse_comic(scan_comic_ctx_t *ctx, vfile_t *f, document_t *doc);

#endif
