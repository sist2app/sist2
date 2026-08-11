#ifndef SCAN_FONT_H
#define SCAN_FONT_H

#include "../scan.h"


typedef struct {
    int enable_tn;
    log_callback_t log;
    logf_callback_t logf;
} scan_font_ctx_t;

void parse_font(scan_font_ctx_t *ctx, vfile_t *f, document_t *doc);
void cleanup_font();

#endif
