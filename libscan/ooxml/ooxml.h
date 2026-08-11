#ifndef SCAN_OOXML_H
#define SCAN_OOXML_H

#include <stdlib.h>
#include "../scan.h"

typedef struct {
    int enable_tn;
    long content_size;
    log_callback_t log;
    logf_callback_t logf;
} scan_ooxml_ctx_t;

void parse_ooxml(scan_ooxml_ctx_t *ctx, vfile_t *f, document_t *doc);

#endif
