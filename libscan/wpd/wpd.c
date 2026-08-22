#include "wpd.h"
#include "libwpd_c_api.h"

/*
 * The WordPerfect document summary carries far more fields than sist2 has room for (account,
 * client, matter, telephone number...); only the three with an equivalent are kept. WordPerfect
 * calls the title of a document its "descriptive name".
 */
void wpd_set_meta(document_t *doc, const char *key, const char *value) {
    if (key == NULL || value == NULL || *value == '\0') {
        return;
    }

    if (strcmp(key, "meta:initial-creator") == 0 || strcmp(key, "dc:creator") == 0) {
        APPEND_UTF8_META(doc, MetaAuthor, value);
    } else if (strcmp(key, "libwpd:descriptive-name") == 0) {
        APPEND_UTF8_META(doc, MetaTitle, value);
    } else if (strcmp(key, "libwpd:editor") == 0) {
        APPEND_UTF8_META(doc, MetaModifiedBy, value);
    }
}

scan_code_t parse_wpd(scan_wpd_ctx_t *ctx, vfile_t *f, document_t *doc) {

    size_t buf_len;
    void *buf = read_all(f, &buf_len);

    void *stream = wpd_memory_stream_create(buf, buf_len);
    wpd_confidence_t conf = wpd_is_file_format_supported(stream);

    if (conf == C_WPD_CONFIDENCE_SUPPORTED_ENCRYPTION || conf == C_WPD_CONFIDENCE_UNSUPPORTED_ENCRYPTION) {
        CTX_LOG_DEBUGF("wpd.c", "File is encrypted! Password-protected WPD files are not supported yet (conf=%d)", conf);
        wpd_memory_stream_destroy(stream);
        free(buf);
        return SCAN_ERR_READ;
    }

    if (conf != C_WPD_CONFIDENCE_EXCELLENT) {
        CTX_LOG_ERRORF("wpd.c", "Unsupported file format! [%s] (conf=%d)", doc->filepath, conf);
        wpd_memory_stream_destroy(stream);
        free(buf);
        return SCAN_ERR_READ;
    }

    text_buffer_t tex = text_buffer_create(-1);
    wpd_result_t res = wpd_parse(stream, &tex, doc);

    if (res != C_WPD_OK) {
        CTX_LOG_ERRORF("wpd.c", "Error while parsing WPD file [%s] (%d)",
                       doc->filepath, res);
    }

    if (tex.dyn_buffer.cur != 0) {
        APPEND_STR_META(doc, MetaContent, tex.dyn_buffer.buf);
    }

    text_buffer_destroy(&tex);
    wpd_memory_stream_destroy(stream);
    free(buf);

    return res == C_WPD_OK ? SCAN_OK : SCAN_ERR_READ;
}
