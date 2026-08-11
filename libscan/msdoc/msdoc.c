#include "msdoc.h"
#include <errno.h>

#include <sys/mman.h>
#include "../../third-party/antiword/src/antiword.h"

void parse_msdoc_text(scan_msdoc_ctx_t *ctx, document_t *doc, FILE *file_in, void *buf, size_t buf_len) {

    // Open word doc
    options_type *opts = direct_vGetOptions();
    opts->iParagraphBreak = 74;
    opts->eConversionType = conversion_text;
    opts->bHideHiddenText = 1;
    opts->bRemoveRemovedText = 1;
    opts->bUseLandscape = 0;
    opts->eEncoding = encoding_utf_8;
    opts->iPageHeight = 842; // A4
    opts->iPageWidth = 595;
    opts->eImageLevel = level_ps_3;

    int doc_word_version = iGuessVersionNumber(file_in, (int) buf_len);
    if (doc_word_version < 0 || doc_word_version == 3) {
        free(buf);
        return;
    }
    rewind(file_in);

    size_t out_len;
    char *out_buf;

    FILE *file_out = open_memstream(&out_buf, &out_len);

    diagram_type *diag = pCreateDiagram("antiword", NULL, file_out);
    if (diag == NULL) {
        fclose(file_in);
        return;
    }

    iInitDocument(file_in, (int) buf_len);
    const char *author = szGetAuthor();
    if (author != NULL) {
        APPEND_UTF8_META(doc, MetaAuthor, author);
    }

    const char *title = szGetTitle();
    if (title != NULL) {
        APPEND_UTF8_META(doc, MetaTitle, title);
    }
    vFreeDocument();

    bWordDecryptor(file_in, (int) buf_len, diag);
    vDestroyDiagram(diag);
    fclose(file_out);

    if (buf_len > 0) {
        text_buffer_t tex = text_buffer_create(ctx->content_size);
        text_buffer_append_string(&tex, out_buf, out_len);
        text_buffer_terminate_string(&tex);

        meta_line_t *meta_content = malloc(sizeof(meta_line_t) + tex.dyn_buffer.cur);
        meta_content->key = MetaContent;
        memcpy(meta_content->str_val, tex.dyn_buffer.buf, tex.dyn_buffer.cur);
        APPEND_META(doc, meta_content);

        text_buffer_destroy(&tex);
    }

    free(buf);
    free(out_buf);
}

void parse_msdoc(scan_msdoc_ctx_t *ctx, vfile_t *f, document_t *doc) {

    size_t buf_len;
    char *buf = read_all(f, &buf_len);
    if (buf == NULL) {
        CTX_LOG_ERROR(f->filepath, "read_all() failed");
        return;
    }

    FILE *file = fmemopen(buf, buf_len, "rb");
    if (file == NULL) {
        free(buf);
        CTX_LOG_ERRORF(f->filepath, "fmemopen() failed (%d)", errno);
        return;
    }

    parse_msdoc_text(ctx, doc, file, buf, buf_len);
    fclose(file);
}
