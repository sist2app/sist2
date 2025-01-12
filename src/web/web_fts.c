#include "serve.h"
#include <mongoose.h>
#include "src/web/web_util.h"

typedef struct {
    int index_id;
    char *prefix;
    int min_depth;
    int max_depth;
} fts_search_paths_req_t;

typedef struct {
    cJSON *val;
    int invalid;
} json_value;

typedef struct {
    char *query;
    char *path;
    fts_sort_t sort;
    double size_min;
    double size_max;
    double date_min;
    double date_max;
    int page_size;
    int *index_ids;
    char **mime_types;
    char **tags;
    int sort_asc;
    int seed;
    char **after;
    int fetch_aggregations;
    int highlight;
    int highlight_context_size;
    int model;
    float *embedding;
    int embedding_size;
} fts_search_req_t;

fts_sort_t get_sort_mode(const cJSON *req_sort) {
    if (strcmp(req_sort->valuestring, "score") == 0) {
        return FTS_SORT_SCORE;
    } else if (strcmp(req_sort->valuestring, "size") == 0) {
        return FTS_SORT_SIZE;
    } else if (strcmp(req_sort->valuestring, "mtime") == 0) {
        return FTS_SORT_MTIME;
    } else if (strcmp(req_sort->valuestring, "random") == 0) {
        return FTS_SORT_RANDOM;
    } else if (strcmp(req_sort->valuestring, "name") == 0) {
        return FTS_SORT_NAME;
    } else if (strcmp(req_sort->valuestring, "embedding") == 0) {
        return FTS_SORT_EMBEDDING;
    }

    return FTS_SORT_INVALID;
}

float *get_float_buffer(cJSON *arr, int *size) {
    *size = cJSON_GetArraySize(arr);

    float *floats = malloc(sizeof(float) * *size);

    cJSON *elem;
    int i = 0;
    cJSON_ArrayForEach(elem, arr) {
        floats[i] = (float) elem->valuedouble;
        i += 1;
    }

    return floats;
}

static json_value get_json_string(cJSON *object, const char *name) {

    cJSON *item = cJSON_GetObjectItem(object, name);
    if (item == NULL || cJSON_IsNull(item)) {
        return (json_value) {NULL, FALSE};
    }
    if (!cJSON_IsString(item)) {
        return (json_value) {NULL, TRUE};
    }

    return (json_value) {item, FALSE};
}

static json_value get_json_number(cJSON *object, const char *name) {

    cJSON *item = cJSON_GetObjectItem(object, name);
    if (item == NULL || cJSON_IsNull(item)) {
        return (json_value) {NULL, FALSE};
    }
    if (!cJSON_IsNumber(item)) {
        return (json_value) {NULL, TRUE};
    }

    return (json_value) {item, FALSE};
}

static json_value get_json_bool(cJSON *object, const char *name) {
    cJSON *item = cJSON_GetObjectItem(object, name);
    if (item == NULL || cJSON_IsNull(item)) {
        return (json_value) {NULL, FALSE};
    }
    if (!cJSON_IsBool(item)) {
        return (json_value) {NULL, TRUE};
    }

    return (json_value) {item, FALSE};
}

static json_value get_json_number_array(cJSON *object, const char *name) {
    cJSON *item = cJSON_GetObjectItem(object, name);
    if (item == NULL || cJSON_IsNull(item)) {
        return (json_value) {NULL, FALSE};
    }
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) == 0) {
        return (json_value) {NULL, TRUE};
    }

    cJSON *elem;
    cJSON_ArrayForEach(elem, item) {
        if (!cJSON_IsNumber(elem)) {
            return (json_value) {NULL, TRUE};
        }
    }

    return (json_value) {item, FALSE};
}

static json_value get_json_array(cJSON *object, const char *name) {
    cJSON *item = cJSON_GetObjectItem(object, name);
    if (item == NULL || cJSON_IsNull(item)) {
        return (json_value) {NULL, FALSE};
    }
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) == 0) {
        return (json_value) {NULL, TRUE};
    }

    cJSON *elem;
    cJSON_ArrayForEach(elem, item) {
        if (!cJSON_IsString(elem)) {
            return (json_value) {NULL, TRUE};
        }
    }

    return (json_value) {item, FALSE};
}

char **json_array_to_c_array(cJSON *json) {
    cJSON *element;
    char **arr = calloc(cJSON_GetArraySize(json) + 1, sizeof(char *));
    int i = 0;
    cJSON_ArrayForEach(element, json) {
        arr[i++] = strdup(element->valuestring);
    }

    return arr;
}

int *json_number_array_to_c_array(cJSON *json) {
    cJSON *element;
    int *arr = calloc(cJSON_GetArraySize(json) + 1, sizeof(int));
    int i = 0;
    cJSON_ArrayForEach(element, json) {
        arr[i++] = (int) element->valuedouble;
    }

    return arr;
}

#define DEFAULT_HIGHLIGHT_CONTEXT_SIZE 20

fts_search_req_t *get_search_req(struct mg_http_message *hm) {
    cJSON *json = web_get_json_body(hm);

    if (json == NULL) {
        return NULL;
    }

    json_value req_query, req_path, req_size_min, req_size_max, req_date_min, req_date_max, req_page_size,
            req_index_ids, req_mime_types, req_tags, req_sort_asc, req_sort, req_seed, req_after,
            req_fetch_aggregations, req_highlight, req_highlight_context_size, req_embedding, req_model,
            req_search_in_path;

    if (!cJSON_IsObject(json) ||
        (req_query = get_json_string(json, "query")).invalid ||
        (req_path = get_json_string(json, "path")).invalid ||
        (req_sort = get_json_string(json, "sort")).val == NULL ||
        (req_size_min = get_json_number(json, "sizeMin")).invalid ||
        (req_size_max = get_json_number(json, "sizeMax")).invalid ||
        (req_date_min = get_json_number(json, "dateMin")).invalid ||
        (req_date_max = get_json_number(json, "dateMax")).invalid ||
        (req_page_size = get_json_number(json, "pageSize")).val == NULL ||
        (req_after = get_json_array(json, "after")).invalid ||
        (req_seed = get_json_number(json, "seed")).invalid ||
        (req_fetch_aggregations = get_json_bool(json, "fetchAggregations")).invalid ||
        (req_sort_asc = get_json_bool(json, "sortAsc")).invalid ||
        (req_index_ids = get_json_number_array(json, "indexIds")).invalid ||
        (req_mime_types = get_json_array(json, "mimeTypes")).invalid ||
        (req_highlight = get_json_bool(json, "highlight")).invalid ||
        (req_search_in_path = get_json_bool(json, "searchInPath")).invalid ||
        (req_highlight_context_size = get_json_number(json, "highlightContextSize")).invalid ||
        (req_embedding = get_json_number_array(json, "embedding")).invalid ||
        (req_model = get_json_number(json, "model")).invalid ||
        (req_tags = get_json_array(json, "tags")).invalid) {
        cJSON_Delete(json);
        return NULL;
    }

    int index_id_count = cJSON_GetArraySize(req_index_ids.val);
    if (index_id_count > 999) {
        cJSON_Delete(json);
        return NULL;
    }
    int mime_count = req_mime_types.val ? 0 : cJSON_GetArraySize(req_mime_types.val);
    if (mime_count > 999) {
        cJSON_Delete(json);
        return NULL;
    }
    int tag_count = req_tags.val ? 0 : cJSON_GetArraySize(req_tags.val);
    if (tag_count > 9999) {
        cJSON_Delete(json);
        return NULL;
    }
    if (req_path.val && (strstr(req_path.val->valuestring, "*") || strlen(req_path.val->valuestring) >= PATH_MAX)) {
        cJSON_Delete(json);
        return NULL;
    }

    fts_sort_t sort = get_sort_mode(req_sort.val);
    if (sort == FTS_SORT_INVALID) {
        cJSON_Delete(json);
        return NULL;
    }

    if (req_after.val && cJSON_GetArraySize(req_after.val) != 2) {
        cJSON_Delete(json);
        return NULL;
    }

    if (req_page_size.val->valueint > 1000 || req_page_size.val->valueint < 0) {
        cJSON_Delete(json);
        return NULL;
    }
    if (req_highlight_context_size.val && req_highlight_context_size.val->valueint < 0) {
        cJSON_Delete(json);
        return NULL;
    }
    if (req_model.val && !req_embedding.val || !req_model.val && req_embedding.val) {
        cJSON_Delete(json);
        return NULL;
    }

    fts_search_req_t *req = malloc(sizeof(fts_search_req_t));

    req->sort = sort;
    req->path = req_path.val ? strdup(req_path.val->valuestring) : NULL;
    req->size_min = req_size_min.val ? req_size_min.val->valuedouble : 0;
    req->size_max = req_size_max.val ? req_size_max.val->valuedouble : 0;
    req->seed = (int) (req_seed.val ? req_seed.val->valuedouble : 0);
    req->date_min = req_date_min.val ? req_date_min.val->valuedouble : 0;
    req->date_max = req_date_max.val ? req_date_max.val->valuedouble : 0;
    req->page_size = (int) req_page_size.val->valuedouble;
    req->sort_asc = req_sort_asc.val ? req_sort_asc.val->valueint : TRUE;
    req->index_ids = req_index_ids.val ? json_number_array_to_c_array(req_index_ids.val) : NULL;
    req->after = req_after.val ? json_array_to_c_array(req_after.val) : NULL;
    req->mime_types = req_mime_types.val ? json_array_to_c_array(req_mime_types.val) : NULL;
    req->tags = req_tags.val ? json_array_to_c_array(req_tags.val) : NULL;
    req->fetch_aggregations = req_fetch_aggregations.val ? req_fetch_aggregations.val->valueint : FALSE;
    req->highlight = req_highlight.val ? req_highlight.val->valueint : FALSE;
    req->highlight_context_size = req_highlight_context_size.val
                                  ? req_highlight_context_size.val->valueint
                                  : DEFAULT_HIGHLIGHT_CONTEXT_SIZE;
    req->model = req_model.val ? req_model.val->valueint : 0;

    if (req_search_in_path.val->valueint == FALSE && req_query.val) {
        if (asprintf(&req->query, "- path : %s", req_query.val->valuestring) == -1) {
            cJSON_Delete(json);
            return NULL;
        }
    } else {
        req->query = req_query.val ? strdup(req_query.val->valuestring) : NULL;
    }

    req->embedding = req_model.val
                     ? get_float_buffer(req_embedding.val, &req->embedding_size)
                     : NULL;

    cJSON_Delete(json);

    return req;
}

void destroy_array(char **array) {
    if (array == NULL) {
        return;
    }
    array_foreach(array) { free(array[i]); }
    free(array);
}

void destroy_search_req(fts_search_req_t *req) {
    free(req->query);
    free(req->path);

    if (req->index_ids) {
        free(req->index_ids);
    }
    destroy_array(req->mime_types);
    destroy_array(req->tags);

    if (req->embedding) {
        free(req->embedding);
    }

    free(req);
}

fts_search_paths_req_t *get_search_paths_req(struct mg_http_message *hm) {
    cJSON *json = web_get_json_body(hm);

    if (json == NULL) {
        return NULL;
    }

    json_value req_index_id, req_min_depth, req_max_depth, req_prefix;

    if (!cJSON_IsObject(json) ||
        (req_index_id = get_json_number(json, "indexId")).invalid ||
        (req_prefix = get_json_string(json, "prefix")).invalid ||
        (req_min_depth = get_json_number(json, "minDepth")).val == NULL ||
        (req_max_depth = get_json_number(json, "maxDepth")).val == NULL) {
        cJSON_Delete(json);
        return NULL;
    }

    fts_search_paths_req_t *req = malloc(sizeof(fts_search_paths_req_t));

    req->index_id = req_index_id.val ? req_index_id.val->valueint : 0;
    req->prefix = req_prefix.val ? strdup(req_prefix.val->valuestring) : NULL;
    req->min_depth = req_min_depth.val->valueint;
    req->max_depth = req_max_depth.val->valueint;

    cJSON_Delete(json);
    return req;
}

void destroy_search_paths_req(fts_search_paths_req_t *req) {
    if (req->prefix) {
        free(req->prefix);
    }
    free(req);
}

void fts_search_paths(struct mg_connection *nc, struct mg_http_message *hm) {

    fts_search_paths_req_t *req = get_search_paths_req(hm);
    if (req == NULL) {
        HTTP_REPLY_BAD_REQUEST
        return;
    }

    cJSON *json = database_fts_get_paths(WebCtx.search_db, req->index_id, req->min_depth,
                                         req->max_depth, req->prefix, req->max_depth == 10000);

    destroy_search_paths_req(req);
    mg_send_json(nc, json);
    cJSON_Delete(json);
}

void fts_search_mimetypes(struct mg_connection *nc, struct mg_http_message *hm) {

    cJSON *json = database_fts_get_mimetypes(WebCtx.search_db);

    mg_send_json(nc, json);
    cJSON_Delete(json);
}

void fts_search_summary_stats(struct mg_connection *nc, UNUSED(struct mg_http_message *hm)) {

    database_summary_stats_t stats = database_fts_get_date_range(WebCtx.search_db);

    cJSON *json = cJSON_CreateObject();

    cJSON_AddNumberToObject(json, "dateMin", stats.date_min);
    cJSON_AddNumberToObject(json, "dateMax", stats.date_max);

    mg_send_json(nc, json);
    cJSON_Delete(json);
}

void fts_search(struct mg_connection *nc, struct mg_http_message *hm) {

    fts_search_req_t *req = get_search_req(hm);
    if (req == NULL) {
        HTTP_REPLY_BAD_REQUEST
        return;
    }

    cJSON *json = database_fts_search(WebCtx.search_db, req->query, req->path,
                                      (long) req->size_min, (long) req->size_max,
                                      (long) req->date_min, (long) req->date_max,
                                      req->page_size, req->index_ids, req->mime_types,
                                      req->tags, req->sort_asc, req->sort, req->seed,
                                      req->after, req->fetch_aggregations, req->highlight,
                                      req->highlight_context_size, req->model,
                                      req->embedding, req->embedding_size);

    if (json == NULL) {
        HTTP_REPLY_BAD_REQUEST
        return;
    }

    destroy_search_req(req);
    mg_send_json(nc, json);
    cJSON_Delete(json);
}

void fts_get_document(struct mg_connection *nc, struct mg_http_message *hm) {

    sist_id_t sid;

    if (hm->uri.len != 24 || !parse_sid(&sid, hm->uri.buf + 7)) {
        LOG_DEBUGF("serve.c", "Invalid /fts/d/ path: %.*s", (int) hm->uri.len, hm->uri.buf);
        HTTP_REPLY_NOT_FOUND
        return;
    }

    cJSON *json = database_fts_get_document(WebCtx.search_db, sid.sid_int64);

    if (!json) {
        HTTP_REPLY_NOT_FOUND
        return;
    }

    mg_send_json(nc, json);
    cJSON_Delete(json);
}

void fts_suggest_tag(struct mg_connection *nc, struct mg_http_message *hm) {
    char *body = web_get_string_body(hm);

    if (body == NULL) {
        HTTP_REPLY_BAD_REQUEST
        return;
    }

    cJSON *json = database_fts_suggest_tag(WebCtx.search_db, body);

    mg_send_json(nc, json);
    cJSON_Delete(json);
    free(body);
}

void fts_get_tags(struct mg_connection *nc, struct mg_http_message *hm) {
    cJSON *json = database_fts_get_tags(WebCtx.search_db);

    mg_send_json(nc, json);
    cJSON_Delete(json);
}
