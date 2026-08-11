#include "text.h"

scan_code_t parse_text(scan_text_ctx_t *ctx, vfile_t *f, document_t *doc) {

    if (ctx->content_size <= 0) {
        return SCAN_OK;
    }

    int to_read = MIN(ctx->content_size, f->st_size);

    if (to_read <= 2) {
        return SCAN_OK;
    }

    char *buf = malloc(to_read);
    int ret = f->read(f, buf, to_read);
    if (ret < 0) {
        CTX_LOG_ERRORF(doc->filepath, "read() returned error code: [%d]", ret);
        free(buf);
        return SCAN_ERR_READ;
    }

    text_buffer_t tex = text_buffer_create(ctx->content_size);

    if ((*(int16_t*)buf) == (int16_t)0xFFFE) {
        text_buffer_append_string16_le(&tex, buf + 2, to_read - 2);
    } else if((*(int16_t*)buf) == (int16_t)0xFEFF) {
        text_buffer_append_string16_be(&tex, buf + 2, to_read - 2);
    } else {
        text_buffer_append_string(&tex, buf, to_read);
    }
    text_buffer_terminate_string(&tex);

    APPEND_STR_META(doc, MetaContent, tex.dyn_buffer.buf);

    free(buf);
    text_buffer_destroy(&tex);

    return SCAN_OK;
}

#define MAX_MARKUP_SIZE (1024 * 1024)

scan_code_t parse_markup(scan_text_ctx_t *ctx, vfile_t *f, document_t *doc) {

    if (ctx->content_size <= 0) {
        return SCAN_OK;
    }

    int to_read = MIN(MAX_MARKUP_SIZE, f->st_size);

    char *buf = malloc(to_read + 1);
    int ret = f->read(f, buf, to_read);
    if (ret < 0) {
        CTX_LOG_ERRORF(doc->filepath, "read() returned error code: [%d]", ret);
        free(buf);
        return SCAN_ERR_READ;
    }

    *(buf + to_read) = '\0';

    text_buffer_t tex = text_buffer_create(ctx->content_size);
    text_buffer_append_markup(&tex, buf);
    text_buffer_terminate_string(&tex);

    APPEND_STR_META(doc, MetaContent, tex.dyn_buffer.buf);

    free(buf);
    text_buffer_destroy(&tex);

    return SCAN_OK;
}
