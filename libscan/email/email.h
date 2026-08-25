#ifndef SCAN_EMAIL_H
#define SCAN_EMAIL_H

#include "../scan.h"

typedef struct {
    int64_t content_size;

    parse_callback_t parse;
    log_callback_t log;
    logf_callback_t logf;

    unsigned int rfc822_mime;
    unsigned int mbox_mime;
} scan_email_ctx_t;

__always_inline
static int is_rfc822(scan_email_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->rfc822_mime;
}

__always_inline
static int is_mbox(scan_email_ctx_t *ctx, unsigned int mime) {
    return mime == ctx->mbox_mime;
}

scan_code_t parse_email(scan_email_ctx_t *ctx, vfile_t *f, document_t *doc);

scan_code_t parse_mbox(scan_email_ctx_t *ctx, vfile_t *f, document_t *doc);

#endif
