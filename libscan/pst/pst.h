#ifndef SCAN_PST_H
#define SCAN_PST_H

#include "../scan.h"

typedef struct {
    int64_t content_size;

    parse_callback_t parse;
    log_callback_t log;
    logf_callback_t logf;

    unsigned int pst_mime;
    unsigned int outlook_mime;
} scan_pst_ctx_t;

/**
 * .pst and .ost, plus the media type libmagic reports for them, which is also the one it reports
 * for a .msg item — parse_pst() checks the file signature before it does anything with it.
 */
__always_inline
static int is_pst(scan_pst_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->pst_mime || mime == ctx->outlook_mime;
}

scan_code_t parse_pst(scan_pst_ctx_t *ctx, vfile_t *f, document_t *doc);

#endif
