#include "msdoc.h"

#include <antiword2.h>

/* The whole document is extracted and then cut to content_size by the text
 * buffer, which collapses runs of whitespace as it copies — capping the
 * parser at content_size instead would leave the buffer short by however
 * much whitespace the document happens to contain. */
#define MSDOC_MAX_TEXT ((size_t) 64 * 1024 * 1024)
#define MSDOC_MAX_MEM ((size_t) 256 * 1024 * 1024)

static void append_meta(document_t *doc, const aw2_doc *aw2, const char *key,
                        enum metakey meta_key) {
    const char *value = aw2_meta(aw2, key);
    if (value != NULL) {
        APPEND_UTF8_META(doc, meta_key, value);
    }
}

void parse_msdoc_buf(scan_msdoc_ctx_t *ctx, document_t *doc, const char *filepath,
                     void *buf, size_t buf_len) {
    const aw2_options opts = {
            .max_text = MSDOC_MAX_TEXT,
            .max_mem = MSDOC_MAX_MEM,
    };
    aw2_doc *aw2 = NULL;
    const aw2_status status = aw2_parse(buf, buf_len, &opts, &aw2);
    free(buf);

    if (status != AW2_OK) {
        CTX_LOG_WARNINGF(filepath, "aw2_parse() failed: %s", aw2_strerror(status));
        return;
    }

    append_meta(doc, aw2, "author", MetaAuthor);
    append_meta(doc, aw2, "title", MetaTitle);
    append_meta(doc, aw2, "last_author", MetaModifiedBy);

    size_t text_len;
    const char *text = aw2_text(aw2, &text_len);
    if (text_len > 0) {
        text_buffer_t tex = text_buffer_create(ctx->content_size);
        text_buffer_append_string(&tex, text, text_len);
        text_buffer_terminate_string(&tex);

        meta_line_t *meta_content = malloc(sizeof(meta_line_t) + tex.dyn_buffer.cur);
        meta_content->key = MetaContent;
        memcpy(meta_content->str_val, tex.dyn_buffer.buf, tex.dyn_buffer.cur);
        APPEND_META(doc, meta_content);

        text_buffer_destroy(&tex);
    }

    aw2_free(aw2);
}

void parse_msdoc(scan_msdoc_ctx_t *ctx, vfile_t *f, document_t *doc) {

    size_t buf_len;
    char *buf = read_all(f, &buf_len);
    if (buf == NULL) {
        CTX_LOG_ERROR(f->filepath, "read_all() failed");
        return;
    }

    parse_msdoc_buf(ctx, doc, f->filepath, buf, buf_len);
}
