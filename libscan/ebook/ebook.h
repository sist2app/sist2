#ifndef SCAN_EBOOK_H
#define SCAN_EBOOK_H

#include "../scan.h"

typedef struct {
    long content_size;
    int tn_size;
    int enable_tn;
    const char *tesseract_lang;
    const char *tesseract_path;

    log_callback_t log;
    logf_callback_t logf;
    int fast_epub_parse;
    int tn_qscale;
} scan_ebook_ctx_t;

void parse_ebook(scan_ebook_ctx_t *ctx, vfile_t *f, const char *mime_str, document_t *doc);

void
parse_ebook_mem(scan_ebook_ctx_t *ctx, void *buf, size_t buf_len, const char *mime_str, document_t *doc, int tn_only);

__always_inline
static int is_epub(const char *mime_string) {
    return strcmp(mime_string, "application/epub+zip") == 0;
}

#endif
