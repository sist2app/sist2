#ifndef SCAN_TEXT_H
#define SCAN_TEXT_H

#include "../scan.h"
#include "../util.h"

typedef struct {
    long content_size;

    log_callback_t log;
    logf_callback_t logf;
} scan_text_ctx_t;

scan_code_t parse_text(scan_text_ctx_t *ctx, vfile_t *f, document_t *doc);

scan_code_t parse_markup(scan_text_ctx_t *ctx, vfile_t *f, document_t *doc);

#endif
