#ifndef SIST2_RAW_H
#define SIST2_RAW_H

#include "../scan.h"

typedef struct {
    log_callback_t log;
    logf_callback_t logf;

    int enable_tn;
    int tn_size;
    int tn_qscale;
} scan_raw_ctx_t;

void parse_raw(scan_raw_ctx_t *ctx, vfile_t *f, document_t *doc);

#endif //SIST2_RAW_H
