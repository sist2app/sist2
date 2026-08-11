#include "json.h"
#include "cjson/cJSON.h"


#define JSON_MAX_FILE_SIZE (1024 * 1024 * 50)

int json_extract_text(cJSON *json, text_buffer_t *tex) {
    if (cJSON_IsObject(json)) {
        for (cJSON *child = json->child; child != NULL; child = child->next) {
            if (json_extract_text(child, tex)) {
                return TRUE;
            }
        }
    } else if (cJSON_IsArray(json)) {
        cJSON *child;
        cJSON_ArrayForEach(child, json) {
            if (json_extract_text(child, tex)) {
                return TRUE;
            }
        }
    } else if (cJSON_IsString(json)) {
        if (text_buffer_append_string0(tex, json->valuestring) == TEXT_BUF_FULL) {
            return TRUE;
        }
        if (text_buffer_append_char(tex, ' ') == TEXT_BUF_FULL) {
            return TRUE;
        }
    }

    return FALSE;
}

scan_code_t parse_json(scan_json_ctx_t *ctx, vfile_t *f, document_t *doc) {

    if (f->st_size > JSON_MAX_FILE_SIZE) {
        CTX_LOG_WARNINGF("json.c", "File larger than maximum allowed [%s]", f->filepath);
        return SCAN_ERR_SKIP;
    }

    size_t buf_len;
    char *buf = read_all(f, &buf_len);

    if (buf == NULL) {
        return SCAN_ERR_READ;
    }

    buf_len += 1;
    buf = realloc(buf, buf_len);
    *(buf + buf_len - 1) = '\0';

    cJSON *json = cJSON_ParseWithOpts(buf, NULL, TRUE);
    text_buffer_t tex = text_buffer_create(ctx->content_size);

    json_extract_text(json, &tex);
    text_buffer_terminate_string(&tex);

    APPEND_STR_META(doc, MetaContent, tex.dyn_buffer.buf);

    cJSON_Delete(json);
    free(buf);
    text_buffer_destroy(&tex);

    return SCAN_OK;
}

#define JSON_BUF_SIZE (1024 * 1024 * 5)

scan_code_t parse_ndjson(scan_json_ctx_t *ctx, vfile_t *f, document_t *doc) {

    char *buf = calloc(JSON_BUF_SIZE + 1, sizeof(char));
    *(buf + JSON_BUF_SIZE) = '\0';

    text_buffer_t tex = text_buffer_create(ctx->content_size);

    size_t ret;
    int eof = FALSE;
    const char *parse_end = buf;
    size_t to_read;
    char *ptr = buf;

    while (TRUE) {
        cJSON *json;

        if (!eof) {
            to_read = parse_end == buf ? JSON_BUF_SIZE : parse_end - buf;
            ret = f->read(f, ptr, to_read);
            if (ret != to_read) {
                eof = TRUE;
            }
        }

        json = cJSON_ParseWithOpts(buf, &parse_end, FALSE);

        if (parse_end == buf + JSON_BUF_SIZE) {
            CTX_LOG_ERRORF("json.c", "Line too large for buffer [%s]", doc->filepath);
            cJSON_Delete(json);
            break;
        }

        if (parse_end == buf) {
            cJSON_Delete(json);
            break;
        }

        json_extract_text(json, &tex);

        cJSON_Delete(json);

        memmove(buf, parse_end, (buf + JSON_BUF_SIZE - parse_end));
        ptr = buf + JSON_BUF_SIZE - parse_end + buf;
    }

    text_buffer_terminate_string(&tex);

    APPEND_STR_META(doc, MetaContent, tex.dyn_buffer.buf);

    free(buf);
    text_buffer_destroy(&tex);
}
