#ifndef SCAN_SCAN_MOBI_H
#define SCAN_SCAN_MOBI_H

#include "../scan.h"

typedef struct {
    long content_size;
    log_callback_t log;
    logf_callback_t logf;

    int tn_qscale;
    int tn_size;
    int enable_tn;
} scan_mobi_ctx_t;

void parse_mobi(scan_mobi_ctx_t *ctx, vfile_t *f, document_t *doc);

#endif
