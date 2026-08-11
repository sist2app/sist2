#include "web_util.h"
#include "src/embed.h"

EMBED_FILE(favicon_ico, SIST2_ROOT "/sist2-vue/src/assets/favicon.ico");
EMBED_FILE(index_html, SIST2_ROOT "/sist2-vue/dist/index.html");
EMBED_FILE(index_js, SIST2_ROOT "/sist2-vue/dist/js/index.js");
EMBED_FILE(chunk_vendors_js, SIST2_ROOT "/sist2-vue/dist/js/chunk-vendors.js");
EMBED_FILE(index_css, SIST2_ROOT "/sist2-vue/dist/css/index.css");
EMBED_FILE(chunk_vendors_css, SIST2_ROOT "/sist2-vue/dist/css/chunk-vendors.css");


void web_serve_asset_index_html(struct mg_connection *nc) {
    web_send_headers(nc, 200, index_html_size, HTTP_CROSS_ORIGIN_HEADERS "Content-Type: text/html");
    mg_send(nc, index_html, index_html_size);
    nc->is_resp = 0;
}

void web_serve_asset_index_js(struct mg_connection *nc) {
    web_send_headers(nc, 200, index_js_size, "Content-Type: application/javascript");
    mg_send(nc, index_js, index_js_size);
    nc->is_resp = 0;
}

void web_serve_asset_chunk_vendors_js(struct mg_connection *nc) {
    web_send_headers(nc, 200, chunk_vendors_js_size, "Content-Type: application/javascript");
    mg_send(nc, chunk_vendors_js, chunk_vendors_js_size);
    nc->is_resp = 0;
}

void web_serve_asset_favicon_ico(struct mg_connection *nc) {
    web_send_headers(nc, 200, favicon_ico_size, "Content-Type: image/x-icon");
    mg_send(nc, favicon_ico, favicon_ico_size);
    nc->is_resp = 0;
}

void web_serve_asset_style_css(struct mg_connection *nc) {
    web_send_headers(nc, 200, index_css_size, "Content-Type: text/css");
    mg_send(nc, index_css, index_css_size);
    nc->is_resp = 0;
}

void web_serve_asset_chunk_vendors_css(struct mg_connection *nc) {
    web_send_headers(nc, 200, chunk_vendors_css_size, "Content-Type: text/css");
    mg_send(nc, chunk_vendors_css, chunk_vendors_css_size);
    nc->is_resp = 0;
}

index_t *web_get_index_by_id(int index_id) {
    for (int i = WebCtx.index_count; i >= 0; i--) {
        if (index_id == WebCtx.indices[i].desc.id) {
            return &WebCtx.indices[i];
        }
    }
    return NULL;
}

database_t *web_get_database(int index_id) {
    index_t *idx = web_get_index_by_id(index_id);
    if (idx != NULL) {
        return idx->db;
    }
    return NULL;
}

void web_send_headers(struct mg_connection *nc, int status_code, size_t length, char *extra_headers) {
    mg_printf(
            nc,
            "HTTP/1.1 %d %s\r\n"
    HTTP_SERVER_HEADER
    "Content-Length: %d\r\n"
    "%s\r\n\r\n",
            status_code, "OK",
            length,
            extra_headers
    );
}
cJSON *web_get_json_body(struct mg_http_message *hm) {
    if (hm->body.len == 0) {
        return NULL;
    }

    char *body = malloc(hm->body.len + 1);
    memcpy(body, hm->body.buf, hm->body.len);
    *(body + hm->body.len) = '\0';
    cJSON *json = cJSON_Parse(body);
    free(body);

    return json;
}

char *web_get_string_body(struct mg_http_message *hm) {
    if (hm->body.len == 0) {
        return NULL;
    }

    char *body = malloc(hm->body.len + 1);
    memcpy(body, hm->body.buf, hm->body.len);
    *(body + hm->body.len) = '\0';

    return body;
}

void mg_send_json(struct mg_connection *nc, const cJSON *json) {
    char *json_str = cJSON_PrintUnformatted(json);

    web_send_headers(nc, 200, strlen(json_str), "Content-Type: application/json");
    mg_send(nc, json_str, strlen(json_str));
    nc->is_resp = 0;

    free(json_str);
}

