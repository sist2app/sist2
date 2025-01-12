#include "serve.h"

#include "src/sist.h"
#include "src/index/elastic.h"
#include "src/index/web.h"
#include "src/auth0/auth0_c_api.h"
#include "src/web/web_util.h"
#include "src/cli.h"
#include <time.h>

#include <src/ctx.h>

void fts_search_paths(struct mg_connection *nc, struct mg_http_message *hm);

void fts_search_mimetypes(struct mg_connection *nc, struct mg_http_message *hm);

void fts_search_summary_stats(struct mg_connection *nc, struct mg_http_message *hm);

void fts_search(struct mg_connection *nc, struct mg_http_message *hm);

void fts_get_document(struct mg_connection *nc, struct mg_http_message *hm);

void fts_suggest_tag(struct mg_connection *nc, struct mg_http_message *hm);

void fts_get_tags(struct mg_connection *nc, struct mg_http_message *hm);

static struct mg_http_serve_opts DefaultServeOpts = {
        .fs = NULL,
        .ssi_pattern = NULL,
        .root_dir = NULL,
        .mime_types = HTTP_SERVER_HEADER
};

static struct mg_http_serve_opts IndexServeOpts = {
        .fs = NULL,
        .ssi_pattern = NULL,
        .root_dir = NULL,
        .mime_types = "",
        .extra_headers = HTTP_SERVER_HEADER HTTP_CROSS_ORIGIN_HEADERS
};

void get_embedding(struct mg_connection *nc, struct mg_http_message *hm) {

    if (WebCtx.search_backend == ES_SEARCH_BACKEND && WebCtx.es_version != NULL && !HAS_KNN(WebCtx.es_version)) {
        LOG_WARNINGF("serve.c",
                     "Your Elasticsearch version (%d.%d.%d) does not support approximate kNN search and will"
                     " fallback to a brute-force search. Please install ES 8.x.x+ for better search performance.",
                     WebCtx.es_version->major, WebCtx.es_version->minor, WebCtx.es_version->patch);
    }

    sist_id_t sid;

    if (hm->uri.len != SIST_SID_LEN + 2 + 4 || !parse_sid(&sid, hm->uri.buf + 3)) {
        LOG_DEBUGF("serve.c", "Invalid embedding path: %.*s", (int) hm->uri.len, hm->uri.buf);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    int model_id = (int) strtol(hm->uri.buf + SIST_SID_LEN + 3, NULL, 10);

    database_t *db = web_get_database(sid.index_id);
    if (db == NULL) {
        LOG_DEBUGF("serve.c", "Could not get database for index: %s", sid.index_id);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    cJSON *json = database_get_embedding(db, sid.doc_id, model_id);

    if (json == NULL) {
        HTTP_REPLY_NOT_FOUND
        return;
    }

    mg_send_json(nc, json);
    cJSON_Delete(json);
}

void stats_files(struct mg_connection *nc, struct mg_http_message *hm) {

    if (hm->uri.len != 17) {
        HTTP_REPLY_NOT_FOUND
        return;
    }

    char index_id_str[9];
    char arg_stat_type[5];

    memcpy(index_id_str, hm->uri.buf + 3, 8);
    *(index_id_str + 8) = '\0';
    int index_id = (int) strtol(index_id_str, NULL, 16);

    memcpy(arg_stat_type, hm->uri.buf + 3 + 9, 4);
    *(arg_stat_type + sizeof(arg_stat_type) - 1) = '\0';

    database_stat_type_d stat_type = database_get_stat_type_by_mnemonic(arg_stat_type);
    if (stat_type == DATABASE_STAT_INVALID) {
        HTTP_REPLY_NOT_FOUND
        return;
    }

    database_t *db = web_get_database(index_id);
    if (db == NULL) {
        LOG_DEBUGF("serve.c", "Could not get database for index: %d", index_id);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    cJSON *json = database_get_stats(db, stat_type);
    mg_send_json(nc, json);
    cJSON_Delete(json);
}

void serve_index_html(struct mg_connection *nc, struct mg_http_message *hm) {
    if (WebCtx.dev) {
        mg_http_serve_file(nc, hm, "sist2-vue/dist/index.html", &IndexServeOpts);
    } else {
        web_serve_asset_index_html(nc);
    }
}

void serve_index_js(struct mg_connection *nc, struct mg_http_message *hm) {
    if (WebCtx.dev) {
        mg_http_serve_file(nc, hm, "sist2-vue/dist/js/index.js", &DefaultServeOpts);
    } else {
        web_serve_asset_index_js(nc);
    }
}

void serve_chunk_vendors_js(struct mg_connection *nc, struct mg_http_message *hm) {
    if (WebCtx.dev) {
        mg_http_serve_file(nc, hm, "sist2-vue/dist/js/chunk-vendors.js", &DefaultServeOpts);
    } else {
        web_serve_asset_chunk_vendors_js(nc);
    }
}

void serve_favicon_ico(struct mg_connection *nc, UNUSED(struct mg_http_message *hm)) {
    web_serve_asset_favicon_ico(nc);
}

void serve_style_css(struct mg_connection *nc, UNUSED(struct mg_http_message *hm)) {
    web_serve_asset_style_css(nc);
}

void serve_chunk_vendors_css(struct mg_connection *nc, UNUSED(struct mg_http_message *hm)) {
    web_serve_asset_chunk_vendors_css(nc);
}

void serve_thumbnail(struct mg_connection *nc, UNUSED(struct mg_http_message *hm), int index_id,
                     int doc_id, int arg_num) {

    database_t *db = web_get_database(index_id);
    if (db == NULL) {
        LOG_DEBUGF("serve.c", "Could not get database for index: %d", index_id);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    size_t data_len = 0;

    void *data = database_read_thumbnail(db, doc_id, arg_num, &data_len);

    if (data_len != 0) {
        web_send_headers(
                nc, 200, data_len,
                "Content-Type: image/jpeg\r\n"
                "Cache-Control: max-age=31536000"
        );
        mg_send(nc, data, data_len);
        nc->is_resp = 0;
        free(data);
    } else {
        HTTP_REPLY_NOT_FOUND
        return;
    }
}

void thumbnail_with_num(struct mg_connection *nc, struct mg_http_message *hm) {
    sist_id_t sid;

    if (hm->uri.len != SIST_SID_LEN + 2 + 4 || !parse_sid(&sid, hm->uri.buf + 3)) {
        LOG_DEBUGF("serve.c", "Invalid thumbnail path: %.*s", (int) hm->uri.len, hm->uri.buf);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    int num = (int) strtol(hm->uri.buf + SIST_SID_LEN + 3, NULL, 10);

    serve_thumbnail(nc, hm, sid.index_id, sid.doc_id, num);
}

void thumbnail(struct mg_connection *nc, struct mg_http_message *hm) {
    sist_id_t sid;

    if (hm->uri.len != 20 || !parse_sid(&sid, hm->uri.buf + 3)) {
        LOG_DEBUGF("serve.c", "Invalid thumbnail path: %.*s", (int) hm->uri.len, hm->uri.buf);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    serve_thumbnail(nc, hm, sid.index_id, sid.doc_id, 0);
}

void search(struct mg_connection *nc, struct mg_http_message *hm) {
    if (hm->body.len == 0) {
        LOG_DEBUG("serve.c", "Client sent empty body, ignoring request");
        mg_http_reply(nc, 400, HTTP_SERVER_HEADER HTTP_TEXT_TYPE_HEADER, "Invalid request");
        return;
    }

    char *body = malloc(hm->body.len + 1);
    memcpy(body, hm->body.buf, hm->body.len);
    *(body + hm->body.len) = '\0';

    char url[4096];
    snprintf(url, 4096, "%s/%s/_search", WebCtx.es_url, WebCtx.es_index);

    nc->fn_data = web_post_async(url, body, WebCtx.es_insecure_ssl);
    nc->is_resp = 1;
}

void serve_file_from_url(cJSON *json, index_t *idx, struct mg_connection *nc) {

    const char *path = cJSON_GetObjectItem(json, "path")->valuestring;
    const char *name = cJSON_GetObjectItem(json, "name")->valuestring;

    char name_unescaped[PATH_MAX * 3];
    str_unescape(name_unescaped, name);

    char path_unescaped[PATH_MAX * 3];
    str_unescape(path_unescaped, path);

    const char *ext = cJSON_GetObjectItem(json, "extension")->valuestring;

    char url[8192];
    snprintf(url, sizeof(url),
             "%s%s/%s%s%s",
             idx->desc.rewrite_url, path_unescaped, name_unescaped, strlen(ext) == 0 ? "" : ".", ext);

    dyn_buffer_t encoded = url_escape(url);
    dyn_buffer_write_char(&encoded, '\0');

    char location_header[8192];
    snprintf(location_header, sizeof(location_header), "Location: %s\r\n", encoded.buf);

    mg_http_reply(nc, 308, location_header, "");
    dyn_buffer_destroy(&encoded);
}

void serve_file_from_disk(cJSON *json, index_t *idx, struct mg_connection *nc, struct mg_http_message *hm) {

    if (strcmp(MG_VERSION, EXPECTED_MONGOOSE_VERSION) != 0) {
        LOG_WARNING("serve.c", "sist2 was not linked with latest mongoose version, "
                               "serving file from disk might not work as expected.");
    }

    const char *path = cJSON_GetObjectItem(json, "path")->valuestring;
    const char *name = cJSON_GetObjectItem(json, "name")->valuestring;
    const char *ext = cJSON_GetObjectItem(json, "extension")->valuestring;
    const char *mime = cJSON_GetObjectItem(json, "mime")->valuestring;

    char name_unescaped[PATH_MAX * 3];
    str_unescape(name_unescaped, name);

    char path_unescaped[PATH_MAX * 3];
    str_unescape(path_unescaped, path);

    char full_path[PATH_MAX];
    snprintf(full_path, PATH_MAX, "%s%s%s%s%s%s",
             idx->desc.root, path_unescaped, strlen(path_unescaped) == 0 ? "" : "/",
             name_unescaped, strlen(ext) == 0 ? "" : ".", ext);

    LOG_DEBUGF("serve.c", "Serving file from disk: %s", full_path);

    char disposition[8192];
    snprintf(disposition, sizeof(disposition),
             HTTP_SERVER_HEADER "Content-Disposition: inline; filename=\"%s%s%s\"\r\n"
             "Accept-Ranges: bytes\r\nCache-Control: no-store\r\n",
             name, strlen(ext) == 0 ? "" : ".", ext);

    char mime_mapping[8192];
    if (strlen(ext) == 0) {
        snprintf(mime_mapping, sizeof(mime_mapping), "%s=%s%s",
                 full_path, mime, STR_STARTS_WITH_CONSTANT(mime, "text/") ? "; charset=utf8" : "");
    } else {
        snprintf(mime_mapping, sizeof(mime_mapping), "%s=%s%s",
                 ext, mime, STR_STARTS_WITH_CONSTANT(mime, "text/") ? "; charset=utf8" : "");
    }

    struct mg_http_serve_opts opts = {
            .extra_headers = disposition,
            .mime_types = mime_mapping
    };
    mg_http_serve_file(nc, hm, full_path, &opts);
}

void cache_es_version() {
    if (WebCtx.search_backend == SQLITE_SEARCH_BACKEND) {
        return;
    }

    static int is_cached = FALSE;

    if (is_cached == TRUE) {
        return;
    }

    es_version_t *es_version = elastic_get_version(WebCtx.es_url, WebCtx.es_insecure_ssl);
    if (es_version != NULL) {
        WebCtx.es_version = es_version;
        is_cached = TRUE;
    }
}

void index_info(struct mg_connection *nc) {

    cache_es_version();

    const char *es_version = "0.0.0";
    if (WebCtx.es_version != NULL) {
        es_version = format_es_version(WebCtx.es_version);
    }

    cJSON *json = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(json, "indices");

    cJSON_AddStringToObject(json, "esIndex", WebCtx.es_index);
    cJSON_AddStringToObject(json, "version", Version);

#ifdef SIST_DEBUG_INFO
    cJSON_AddStringToObject(json, "mongooseVersion", MG_VERSION);
    cJSON_AddStringToObject(json, "esVersion", es_version);
    cJSON_AddStringToObject(json, "platform", QUOTE(SIST_PLATFORM));
    cJSON_AddStringToObject(json, "sist2Hash", Sist2CommitHash);
    cJSON_AddBoolToObject(json, "dev", WebCtx.dev);
    cJSON_AddBoolToObject(json, "showDebugInfo", TRUE);
#else
    cJSON_AddBoolToObject(json, "showDebugInfo", FALSE);
#endif

    cJSON_AddBoolToObject(json, "esVersionSupported", IS_SUPPORTED_ES_VERSION(WebCtx.es_version));
    cJSON_AddBoolToObject(json, "esVersionLegacy", IS_LEGACY_VERSION(WebCtx.es_version));
    cJSON_AddBoolToObject(json, "esVersionHasKnn", HAS_KNN(WebCtx.es_version));
    cJSON_AddStringToObject(json, "lang", WebCtx.lang);

    cJSON_AddBoolToObject(json, "auth0Enabled", WebCtx.auth0_enabled);
    if (WebCtx.auth0_enabled) {
        cJSON_AddStringToObject(json, "auth0Domain", WebCtx.auth0_domain);
        cJSON_AddStringToObject(json, "auth0ClientId", WebCtx.auth0_client_id);
        cJSON_AddStringToObject(json, "auth0Audience", WebCtx.auth0_audience);
    }

#ifdef SIST_DEBUG
    cJSON_AddBoolToObject(json, "debug", TRUE);
#else
    cJSON_AddBoolToObject(json, "debug", FALSE);
#endif
    cJSON_AddStringToObject(json, "tagline", WebCtx.tagline);

    for (int i = 0; i < WebCtx.index_count; i++) {
        index_t *idx = &WebCtx.indices[i];

        cJSON *idx_json = cJSON_CreateObject();
        cJSON_AddStringToObject(idx_json, "name", idx->desc.name);
        cJSON_AddStringToObject(idx_json, "version", idx->desc.version);
        cJSON_AddNumberToObject(idx_json, "id", idx->desc.id);
        cJSON_AddStringToObject(idx_json, "rewriteUrl", idx->desc.rewrite_url);
        cJSON_AddNumberToObject(idx_json, "timestamp", (double) idx->desc.timestamp);
        cJSON_AddItemToArray(arr, idx_json);

#ifdef SIST_DEBUG_INFO
        cJSON_AddStringToObject(idx_json, "root", idx->desc.root);
#endif

        cJSON *models = database_get_models(idx->db);
        cJSON_AddItemToObject(idx_json, "models", models);
    }

    if (WebCtx.search_backend == SQLITE_SEARCH_BACKEND) {
        cJSON_AddStringToObject(json, "searchBackend", "sqlite");
    } else {
        cJSON_AddStringToObject(json, "searchBackend", "elasticsearch");
    }

    mg_send_json(nc, json);
    cJSON_Delete(json);
}

cJSON *get_root_document_by_id(int index_id, int doc_id) {

    database_t *db = web_get_database(index_id);
    if (!db) {
        return NULL;
    }

    int next_id = doc_id;

    while (TRUE) {
        cJSON *doc = database_get_document(db, next_id);

        if (doc == NULL) {
            return NULL;
        }

        cJSON *parent = cJSON_GetObjectItem(doc, "parent");
        if (parent == NULL || !cJSON_IsNumber(parent)) {
            return doc;
        }

        next_id = parent->valueint;
        cJSON_Delete(doc);
    }
}

void file(struct mg_connection *nc, struct mg_http_message *hm) {
    sist_id_t sid;

    if (hm->uri.len != 20 || !parse_sid(&sid, hm->uri.buf + 3)) {
        LOG_DEBUGF("serve.c", "Invalid file path: %.*s", (int) hm->uri.len, hm->uri.buf);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    index_t *idx = web_get_index_by_id(sid.index_id);
    if (idx == NULL) {
        HTTP_REPLY_NOT_FOUND
        return;
    }

    cJSON *source = get_root_document_by_id(sid.index_id, sid.doc_id);

    if (source == NULL) {
        HTTP_REPLY_NOT_FOUND
        return;
    }

    if (strlen(idx->desc.rewrite_url) == 0) {
        serve_file_from_disk(source, idx, nc, hm);
    } else {
        serve_file_from_url(source, idx, nc);
    }
    cJSON_Delete(source);
}

void status(struct mg_connection *nc) {
    char *status = elastic_get_status();
    if (strcmp(status, "open") == 0) {
        web_send_headers(nc, 204, 0, "Content-Type: application/json");
    } else {
        web_send_headers(nc, 500, 0, "Content-Type: application/json");
    }

    free(status);
    nc->is_resp = 0;
}

typedef struct {
    char *name;
    int delete;
} tag_req_t;

tag_req_t *parse_tag_request(cJSON *json) {

    if (json == NULL) {
        return NULL;
    }

    if (!cJSON_IsObject(json)) {
        return NULL;
    }

    cJSON *arg_name = cJSON_GetObjectItem(json, "name");
    if (arg_name == NULL || !cJSON_IsString(arg_name)) {
        return NULL;
    }

    cJSON *arg_delete = cJSON_GetObjectItem(json, "delete");
    if (arg_delete == NULL || !cJSON_IsBool(arg_delete)) {
        return NULL;
    }

    tag_req_t *req = malloc(sizeof(tag_req_t));
    req->delete = arg_delete->valueint;
    req->name = arg_name->valuestring;

    return req;
}

subreq_ctx_t *elastic_delete_tag(const char *sid, const tag_req_t *req) {
    char *buf = malloc(sizeof(char) * 8192);
    snprintf(buf, 8192,
             "{"
             "    \"script\" : {"
             "        \"source\": \"if (ctx._source.tag.contains(params.tag)) { ctx._source.tag.remove(ctx._source.tag.indexOf(params.tag)) }\","
             "        \"lang\": \"painless\","
             "        \"params\" : {"
             "            \"tag\" : \"%s\""
             "        }"
             "    }"
             "}", req->name
    );

    char url[4096];
    snprintf(url, sizeof(url), "%s/%s/_update/%s", WebCtx.es_url, WebCtx.es_index, sid);

    return web_post_async(url, buf, WebCtx.es_insecure_ssl);
}

subreq_ctx_t *elastic_write_tag(const char *sid, const tag_req_t *req) {
    char *buf = malloc(sizeof(char) * 8192);
    snprintf(buf, 8192,
             "{"
             "    \"script\" : {"
             "        \"source\": \"if(ctx._source.tag == null) {ctx._source.tag = new ArrayList()} ctx._source.tag.add(params.tag)\","
             "        \"lang\": \"painless\","
             "        \"params\" : {"
             "            \"tag\" : \"%s\""
             "        }"
             "    }"
             "}", req->name
    );

    char url[4096];
    snprintf(url, sizeof(url), "%s/%s/_update/%s", WebCtx.es_url, WebCtx.es_index, sid);
    return web_post_async(url, buf, WebCtx.es_insecure_ssl);
}

void tag(struct mg_connection *nc, struct mg_http_message *hm) {
    sist_id_t sid;
    if (hm->uri.len != 22 || !parse_sid(&sid, hm->uri.buf + 5)) {
        LOG_DEBUGF("serve.c", "Invalid tag path: %.*s", (int) hm->uri.len, hm->uri.buf);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    char *body = malloc(hm->body.len + 1);
    memcpy(body, hm->body.buf, hm->body.len);
    *(body + hm->body.len) = '\0';
    cJSON *json = cJSON_Parse(body);
    free(body);

    if (json == NULL) {
        HTTP_REPLY_BAD_REQUEST
        return;
    }

    database_t *db = web_get_database(sid.index_id);
    if (db == NULL) {
        LOG_DEBUGF("serve.c", "Could not get database for index: %d", sid.index_id);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    tag_req_t *req = parse_tag_request(json);
    if (req == NULL) {
        LOG_DEBUG("serve.c", "Could not parse tag request");
        cJSON_Delete(json);
        HTTP_REPLY_BAD_REQUEST
        return;
    }

    if (req->delete) {
        database_delete_tag(db, sid.doc_id, req->name);
        if (WebCtx.search_backend == SQLITE_SEARCH_BACKEND) {
            database_delete_tag(WebCtx.search_db, sid.sid_int64, req->name);
            HTTP_REPLY_OK
        } else {
            nc->fn_data = elastic_delete_tag(sid.sid_str, req);
        }
    } else {
        database_write_tag(db, sid.doc_id, req->name);
        if (WebCtx.search_backend == SQLITE_SEARCH_BACKEND) {
            database_fts_write_tag(WebCtx.search_db, sid.sid_int64, req->name);
            HTTP_REPLY_OK
        } else {
            nc->fn_data = elastic_write_tag(sid.sid_str, req);
        }
    }

    free(req);
    cJSON_Delete(json);
}

int validate_auth(struct mg_connection *nc, struct mg_http_message *hm) {
    char user[256] = {0,};
    char pass[256] = {0,};

    mg_http_creds(hm, user, sizeof(user), pass, sizeof(pass));
    if (strcmp(user, WebCtx.auth_user) != 0 || strcmp(pass, WebCtx.auth_pass) != 0) {
        mg_http_reply(nc, 401, "WWW-Authenticate: Basic realm=\"sist2\"\r\n", "");
        return FALSE;
    }
    return TRUE;
}

int check_auth0(struct mg_http_message *hm) {

    struct mg_str *cookie = mg_http_get_header(hm, "Cookie");
    if (cookie == NULL) {
        LOG_WARNING("serve.c", "Unauthorized request (no auth cookie)");
        return FALSE;
    }

    struct mg_str token = mg_str("");
    char *token_str = NULL;

    token = mg_http_get_header_var(*cookie, mg_str("sist2-auth0"));
    if (token.len == 0) {
        LOG_WARNING("serve.c", "Unauthorized request (no auth cookie)");
        return FALSE;
    }

    token_str = malloc(token.len + 1);
    strncpy(token_str, token.buf, token.len);
    *(token_str + token.len) = '\0';

    int res = auth0_verify_jwt(
            WebCtx.auth0_public_key,
            token_str,
            WebCtx.auth0_audience
    );
    free(token_str);

    if (res != AUTH0_OK) {
        LOG_WARNINGF("serve.c", "Unauthorized request (JWT validation error: %d)", res);
        return FALSE;
    }

    return TRUE;
}

static void ev_router(struct mg_connection *nc, int ev, void *ev_data) {

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        if (WebCtx.auth_enabled == TRUE) {
            if (!validate_auth(nc, hm)) {
                return;
            }
        }

        char uri[256];
        memcpy(uri, hm->uri.buf, hm->uri.len);
        *(uri + hm->uri.len) = '\0';
        LOG_DEBUGF("serve.c", "<%s> GET %s",
                   web_address_to_string(&(nc->rem)),
                   uri
        );

#define mg_http_match_uri(hm, pattern) mg_match((hm)->uri, mg_str(pattern), NULL)

        if (mg_http_match_uri(hm, "/")) {
            serve_index_html(nc, hm);
            return;
        } else if (mg_http_match_uri(hm, "/favicon.ico")) {
            serve_favicon_ico(nc, hm);
            return;
        } else if (mg_http_match_uri(hm, "/css/index.css")) {
            serve_style_css(nc, hm);
            return;
        } else if (mg_http_match_uri(hm, "/css/chunk-vendors.css")) {
            serve_chunk_vendors_css(nc, hm);
            return;
        } else if (mg_http_match_uri(hm, "/js/index.js")) {
            serve_index_js(nc, hm);
            return;
        } else if (mg_http_match_uri(hm, "/js/chunk-vendors.js")) {
            serve_chunk_vendors_js(nc, hm);
            return;
        } else if (mg_http_match_uri(hm, "/i")) {
            index_info(nc);
            return;
        }

        if (WebCtx.auth0_enabled && !check_auth0(hm)) {
            mg_http_reply(nc, 403, HTTP_SERVER_HEADER HTTP_TEXT_TYPE_HEADER, "Unauthorized (auth0 error)");
            return;
        }

        if (WebCtx.search_backend == SQLITE_SEARCH_BACKEND) {
            if (mg_http_match_uri(hm, "/fts/paths")) {
                fts_search_paths(nc, hm);
                return;
            } else if (mg_http_match_uri(hm, "/fts/mimetypes")) {
                fts_search_mimetypes(nc, hm);
                return;
            } else if (mg_http_match_uri(hm, "/fts/dateRange")) {
                fts_search_summary_stats(nc, hm);
                return;
            } else if (mg_http_match_uri(hm, "/fts/search")) {
                fts_search(nc, hm);
                return;
            } else if (mg_http_match_uri(hm, "/fts/d/*")) {
                fts_get_document(nc, hm);
                return;
            } else if (mg_http_match_uri(hm, "/fts/suggestTags")) {
                fts_suggest_tag(nc, hm);
                return;
            } else if (mg_http_match_uri(hm, "/fts/tags")) {
                fts_get_tags(nc, hm);
                return;
            }
        } else if (WebCtx.search_backend == ES_SEARCH_BACKEND) {
            if (mg_http_match_uri(hm, "/es")) {
                search(nc, hm);
                return;
            }
        }

        if (mg_http_match_uri(hm, "/status")) {
            status(nc);
        } else if (mg_http_match_uri(hm, "/f/*")) {
            file(nc, hm);
        } else if (mg_http_match_uri(hm, "/t/*/*")) {
            thumbnail_with_num(nc, hm);
        } else if (mg_http_match_uri(hm, "/t/*")) {
            thumbnail(nc, hm);
        } else if (mg_http_match_uri(hm, "/s/*/*")) {
            stats_files(nc, hm);
        } else if (mg_http_match_uri(hm, "/tag/*")) {
            if (WebCtx.tag_auth_enabled == TRUE && !validate_auth(nc, hm)) {
                return;
            }
            tag(nc, hm);
        } else if (mg_http_match_uri(hm, "/e/*/*")) {
            get_embedding(nc, hm);
            return;
        } else {
            HTTP_REPLY_NOT_FOUND
        }

    } else if (ev == MG_EV_POLL) {
        if (nc->fn_data != NULL) {
            //Waiting for ES reply
            subreq_ctx_t *ctx = (subreq_ctx_t *) nc->fn_data;
            web_post_async_poll(ctx);

            if (ctx->done == TRUE) {
                response_t *r = ctx->response;

                if (r->status_code == 200) {
                    web_send_headers(nc, 200, r->size, "Content-Type: application/json");
                    mg_send(nc, r->body, r->size);
                    nc->is_resp = 0;
                } else if (r->status_code == 0) {
                    sist_log("serve.c", LOG_SIST_ERROR, "Could not connect to elasticsearch!");

                    mg_http_reply(nc, 503, HTTP_SERVER_HEADER HTTP_TEXT_TYPE_HEADER,
                                  "Elasticsearch connection error, see server logs.");
                } else {
                    sist_logf("serve.c", LOG_SIST_WARNING, "ElasticSearch error during query (%d)", r->status_code);
                    if (r->size != 0) {
                        char *tmp = malloc(r->size + 1);
                        memcpy(tmp, r->body, r->size);
                        *(tmp + r->size) = '\0';
                        cJSON *json = cJSON_Parse(tmp);
                        char *json_str = cJSON_Print(json);
                        sist_log("serve.c", LOG_SIST_WARNING, json_str);
                        free(json_str);
                        free(tmp);
                    }

                    mg_http_reply(nc, 500, HTTP_SERVER_HEADER HTTP_TEXT_TYPE_HEADER,
                                  "Elasticsearch error, see server logs.");
                }

                free_response(r);
                free(ctx->data);
                free(ctx);
                nc->fn_data = NULL;
            }
        }
    }
}

void serve(const char *listen_address) {

    LOG_INFOF("serve.c", "Starting web server @ http://%s", listen_address);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    struct mg_connection *nc = mg_http_listen(&mgr, listen_address, ev_router, NULL);
    if (nc == NULL) {
        LOG_FATALF("serve.c", "Couldn't bind web server on address %s", listen_address);
    }

    while (TRUE) {
        mg_mgr_poll(&mgr, 10);
    }
    mg_mgr_free(&mgr);
    LOG_INFO("serve.c", "Finished web event loop");
}
