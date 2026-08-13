#include "elastic.h"
#include "src/ctx.h"

#include "web.h"

#include "src/embed.h"

EMBED_FILE(mappings_json, SIST2_ROOT "/schema/mappings.json");
EMBED_FILE(settings_json, SIST2_ROOT "/schema/settings.json");
EMBED_FILE(settings_legacy_json, SIST2_ROOT "/schema/settings_legacy.json");
EMBED_FILE(pipeline_json, SIST2_ROOT "/schema/pipeline.json");


typedef struct es_indexer {
    int queued;
    char *es_url;
    char *es_index;
    es_bulk_line_t *line_head;
    es_bulk_line_t *line_tail;
} es_indexer_t;


static __thread es_indexer_t *Indexer = NULL;

void free_queue(int max);

void elastic_flush();

void print_error(response_t *r);

static void mappings_url(char *url, size_t url_size);

void destroy_indexer(es_indexer_t *indexer) {

    if (indexer == NULL) {
        return;
    }

    LOG_DEBUG("elastic.c", "Destroying indexer");

    if (indexer->es_url != NULL) {
        free(indexer->es_url);
        free(indexer->es_index);
    }

    free(indexer);
}

void elastic_cleanup() {
    if (IndexCtx.needs_es_connection) {
        elastic_flush();
    }

    destroy_indexer(Indexer);
}

void print_json(cJSON *document, const char id_str[SIST_SID_LEN]) {

    cJSON *line = cJSON_CreateObject();

    cJSON_AddStringToObject(line, "_id", id_str);
    cJSON_AddStringToObject(line, "_index", IndexCtx.es_index);
//    cJSON_AddStringToObject(line, "_type", "_doc");
    cJSON_AddItemReferenceToObject(line, "_source", document);

    char *json = cJSON_PrintUnformatted(line);

    printf("%s\n", json);

    cJSON_free(json);
    cJSON_Delete(line);
}

void delete_document(const char *sid) {
    es_bulk_line_t *bulk_line = malloc(sizeof(es_bulk_line_t));

    bulk_line->type = ES_BULK_LINE_DELETE;
    bulk_line->next = NULL;
    strcpy(bulk_line->sid, sid);

    // The pool takes ownership of the line
    thread_pool_submit(IndexCtx.pool, bulk_line);
}


void index_json(cJSON *document, const char doc_id[SIST_SID_LEN]) {
    char *json = cJSON_PrintUnformatted(document);

    size_t json_len = strlen(json);
    es_bulk_line_t *bulk_line = malloc(sizeof(es_bulk_line_t) + json_len + 2);
    bulk_line->type = ES_BULK_LINE_INDEX;
    memcpy(bulk_line->line, json, json_len);
    strcpy(bulk_line->sid, doc_id);
    *(bulk_line->line + json_len) = '\n';
    *(bulk_line->line + json_len + 1) = '\0';
    bulk_line->next = NULL;

    cJSON_free(json);

    // The pool takes ownership of the line
    thread_pool_submit(IndexCtx.pool, bulk_line);
}

void *create_bulk_buffer(int max, int *count, size_t *buf_len, int legacy) {
    es_bulk_line_t *line = Indexer->line_head;
    *count = 0;

    size_t buf_size = 0;
    size_t buf_cur = 0;
    char *buf = malloc(8192);
    size_t buf_capacity = 8192;
#define GROW_BUF(delta)                       \
    while (buf_size + (delta) > buf_capacity) { \
      buf_capacity *= 2;                        \
      buf = realloc(buf, buf_capacity);         \
    }                                           \
    buf_size += (delta);                        \

    // see: https://www.elastic.co/guide/en/elasticsearch/reference/current/docs-bulk.html
    // ES_BULK_LINE_INDEX: two lines, 1st action, 2nd content
    // ES_BULK_LINE_DELETE: one line
    while (line != NULL && *count < max) {
        char action_str[256];
        if (line->type == ES_BULK_LINE_INDEX) {

            if (legacy) {
                snprintf(
                        action_str, sizeof(action_str),
                        "{\"index\":{\"_id\":\"%s\",\"_type\":\"_doc\",\"_index\":\"%s\"}}\n",
                        line->sid, Indexer->es_index
                );
            } else {
                snprintf(
                        action_str, sizeof(action_str),
                        "{\"index\":{\"_id\":\"%s\",\"_index\":\"%s\"}}\n",
                        line->sid, Indexer->es_index
                );
            }

            size_t action_str_len = strlen(action_str);
            size_t line_len = strlen(line->line);

            GROW_BUF(action_str_len + line_len);

            memcpy(buf + buf_cur, action_str, action_str_len);
            buf_cur += action_str_len;
            memcpy(buf + buf_cur, line->line, line_len);
            buf_cur += line_len;

        } else if (line->type == ES_BULK_LINE_DELETE) {
            snprintf(
                    action_str, sizeof(action_str),
                    "{\"delete\":{\"_id\":\"%s\",\"_index\":\"%s\"}}\n",
                    line->sid, Indexer->es_index
            );

            size_t action_str_len = strlen(action_str);
            GROW_BUF(action_str_len);
            memcpy(buf + buf_cur, action_str, action_str_len);
            buf_cur += action_str_len;
        }
        line = line->next;
        (*count)++;
    }

    if (buf_size + 1 > buf_capacity) {
        buf = realloc(buf, buf_capacity + 1);
    }

    *(buf + buf_cur) = '\0';

    *buf_len = buf_cur;
    return buf;
}

/** Number of documents Elasticsearch rejected within an otherwise successful bulk request */
int print_errors(response_t *r) {
    char *tmp = malloc(r->size + 1);
    memcpy(tmp, r->body, r->size);
    *(tmp + r->size) = '\0';

    int rejected = 0;

    cJSON *ret_json = cJSON_Parse(tmp);
    cJSON *errors = cJSON_GetObjectItem(ret_json, "errors");

    if (errors == NULL) {
        char *str = cJSON_Print(ret_json);
        LOG_ERRORF("elastic.c", "%s\n", str);
        cJSON_free(str);
    } else if (errors->valueint != 0) {
        cJSON *err;
        cJSON_ArrayForEach(err, cJSON_GetObjectItem(ret_json, "items")) {

            int status = cJSON_GetObjectItem(cJSON_GetObjectItem(err, "index"), "status")->valueint;

            if (status != 201 && status != 200) {
                rejected += 1;

                char *str = cJSON_Print(err);
                LOG_ERRORF("elastic.c", "%s\n", str);
                cJSON_free(str);
            }
        }
    }
    cJSON_Delete(ret_json);
    free(tmp);

    return rejected;
}

void print_error(response_t *r) {
    char *tmp = malloc(r->size + 1);
    memcpy(tmp, r->body, r->size);
    *(tmp + r->size) = '\0';

    cJSON *ret_json = cJSON_Parse(tmp);
    if (cJSON_GetObjectItem(ret_json, "error") != NULL) {
        char *str = cJSON_Print(cJSON_GetObjectItem(ret_json, "error"));
        LOG_ERRORF("elastic.c", "%s\n", str);
        cJSON_free(str);
    }
    cJSON_Delete(ret_json);
    free(tmp);
}

void _elastic_flush(int max) {

    // Every indexing thread flushes its own queue, so an empty one is business as usual
    if (max == 0) {
        return;
    }

    size_t buf_len;
    int count;
    void *buf = create_bulk_buffer(max, &count, &buf_len, IS_LEGACY_VERSION(IndexCtx.es_version));

    char bulk_url[4096];
    snprintf(bulk_url, sizeof(bulk_url), "%s/%s/_bulk?pipeline=tie", Indexer->es_url, Indexer->es_index);
    response_t *r = web_post(bulk_url, buf, IndexCtx.es_insecure_ssl);

    if (r->status_code == 0) {
        LOG_FATALF("elastic.c", "Could not connect to %s, make sure that elasticsearch is running!\n", IndexCtx.es_url);
    }

    if (r->status_code == 413) {

        if (max <= 1) {
            LOG_ERRORF("elastic.c", "Single document too large, giving up: {%s}", Indexer->line_head->sid);
            free_response(r);
            free(buf);
            free_queue(1);
            if (Indexer->queued != 0) {
                elastic_flush();
            }
            return;
        }

        LOG_WARNINGF("elastic.c", "Payload too large, retrying (%d documents)", count);

        free_response(r);
        free(buf);
        _elastic_flush(max / 2);
        return;

    } else if (r->status_code == 429) {

        free_response(r);
        free(buf);
        LOG_WARNING("elastic.c", "Got 429 status, will retry after delay");
        usleep(1000000 * 20);
        _elastic_flush(max);
        return;

    } else if (r->status_code != 200) {
        print_errors(r);
        LOG_ERRORF("elastic.c", "Dropped %d documents after a <%d> response", Indexer->queued, r->status_code);
        free_queue(Indexer->queued);

    } else {

        const int rejected = print_errors(r);

        if (rejected != 0) {
            LOG_ERRORF("elastic.c", "Elasticsearch rejected %d of %d documents", rejected, count);
        }

        LOG_DEBUGF("elastic.c", "Indexed %d documents (%zukB) <%d>", count - rejected, buf_len / 1024,
                   r->status_code);
        free_queue(max);

        if (Indexer->queued != 0) {
            elastic_flush();
        }
    }

    free_response(r);
    free(buf);
}

void free_queue(int max) {
    for (int i = 0; i < max; i++) {
        es_bulk_line_t *tmp = Indexer->line_head;
        Indexer->line_head = tmp->next;
        if (Indexer->line_head == NULL) {
            Indexer->line_tail = NULL;
        }
        free(tmp);
        Indexer->queued -= 1;
    }
}

void elastic_flush() {

    if (Indexer == NULL) {
        Indexer = create_indexer(IndexCtx.es_url, IndexCtx.es_index);
    }

    _elastic_flush(Indexer->queued);
}

void elastic_index_line(es_bulk_line_t *line) {

    if (Indexer == NULL) {
        Indexer = create_indexer(IndexCtx.es_url, IndexCtx.es_index);
    }

    if (Indexer->line_head == NULL) {
        Indexer->line_head = line;
        Indexer->line_tail = Indexer->line_head;
    } else {
        Indexer->line_tail->next = line;
        Indexer->line_tail = line;
    }

    Indexer->queued += 1;

    if (Indexer->queued >= IndexCtx.batch_size) {
        elastic_flush();
    }
}

es_indexer_t *create_indexer(const char *url, const char *index) {

    es_indexer_t *indexer = malloc(sizeof(es_indexer_t));

    if (IndexCtx.needs_es_connection) {
        char *es_url = malloc(strlen(url) + 1);
        strcpy(es_url, url);

        char *es_index = malloc(strlen(index) + 1);
        strcpy(es_index, index);

        indexer->es_url = es_url;
        indexer->es_index = es_index;
    } else {
        indexer->es_url = NULL;
        indexer->es_index = NULL;
    }

    indexer->queued = 0;
    indexer->line_head = NULL;
    indexer->line_tail = NULL;

    return indexer;
}

void finish_indexer(UNUSED(int index_id)) {

    char url[4096];

    snprintf(url, sizeof(url), "%s/%s/_refresh", IndexCtx.es_url, IndexCtx.es_index);
    response_t *r = web_post(url, "", IndexCtx.es_insecure_ssl);
    LOG_INFOF("elastic.c", "Refresh index <%d>", r->status_code);
    free_response(r);

    snprintf(url, sizeof(url), "%s/%s/_forcemerge", IndexCtx.es_url, IndexCtx.es_index);
    r = web_post(url, "", IndexCtx.es_insecure_ssl);
    LOG_INFOF("elastic.c", "Merge index <%d>", r->status_code);
    free_response(r);

    snprintf(url, sizeof(url), "%s/%s/_settings", IndexCtx.es_url, IndexCtx.es_index);
    r = web_put(url, "{\"index\":{\"refresh_interval\":\"1s\"}}", IndexCtx.es_insecure_ssl);
    LOG_INFOF("elastic.c", "Set refresh interval <%d>", r->status_code);
    free_response(r);
}

es_version_t *elastic_get_version(const char *es_url, int insecure) {
    response_t *r = web_get(es_url, 30, insecure);

    char *tmp = malloc(r->size + 1);
    memcpy(tmp, r->body, r->size);
    *(tmp + r->size) = '\0';
    cJSON *response = cJSON_Parse(tmp);
    free(tmp);

    if (response == NULL) {
        return NULL;
    }

    if (cJSON_GetObjectItem(response, "error") != NULL) {
        LOG_WARNING("elastic.c", "Could not get Elasticsearch version");
        print_error(r);
        free_response(r);
        return NULL;
    }

    free_response(r);

    if (cJSON_GetObjectItem(response, "version") == NULL ||
        cJSON_GetObjectItem(cJSON_GetObjectItem(response, "version"), "number") == NULL) {
        cJSON_Delete(response);
        return NULL;
    }

    char *version_str = cJSON_GetObjectItem(cJSON_GetObjectItem(response, "version"), "number")->valuestring;

    es_version_t *version = malloc(sizeof(es_version_t));

    const char *tok = strtok(version_str, ".");
    version->major = atoi(tok);
    tok = strtok(NULL, ".");
    version->minor = atoi(tok);
    tok = strtok(NULL, ".");
    version->patch = atoi(tok);

    cJSON_Delete(response);

    return version;
}

void elastic_init(int force_reset, const char *user_mappings, const char *user_settings) {

    es_version_t *es_version = elastic_get_version(IndexCtx.es_url, IndexCtx.es_insecure_ssl);
    IndexCtx.es_version = es_version;

    if (es_version == NULL) {
        LOG_FATAL("elastic.c", "Could not get ES version");
    }

    LOG_INFOF("elastic.c",
              "Elasticsearch version is %s (supported=%d, legacy=%d)",
              format_es_version(es_version), IS_SUPPORTED_ES_VERSION(es_version), IS_LEGACY_VERSION(es_version));

    if (!IS_SUPPORTED_ES_VERSION(es_version)) {
        LOG_FATAL("elastic.c", "This elasticsearch version is not supported!");
    }

    const char *settings = NULL;
    if (IS_LEGACY_VERSION(es_version)) {
        settings = settings_legacy_json;
    } else {
        settings = settings_json;
    }

    // Check if index exists
    char url[4096];
    snprintf(url, sizeof(url), "%s/%s", IndexCtx.es_url, IndexCtx.es_index);
    response_t *r = web_get(url, 30, IndexCtx.es_insecure_ssl);
    int index_exists = r->status_code == 200;
    free_response(r);

    if (!index_exists || force_reset) {
        r = web_delete(url, IndexCtx.es_insecure_ssl);
        LOG_INFOF("elastic.c", "Delete index <%d>", r->status_code);
        free_response(r);

        snprintf(url, sizeof(url), "%s/%s", IndexCtx.es_url, IndexCtx.es_index);
        r = web_put(url, "", IndexCtx.es_insecure_ssl);

        if (r->status_code != 200) {
            print_error(r);
            LOG_FATAL("elastic.c", "Could not create index");
        }

        LOG_INFOF("elastic.c", "Create index <%d>", r->status_code);
        free_response(r);

        snprintf(url, sizeof(url), "%s/%s/_close", IndexCtx.es_url, IndexCtx.es_index);
        r = web_post(url, "", IndexCtx.es_insecure_ssl);
        LOG_INFOF("elastic.c", "Close index <%d>", r->status_code);
        free_response(r);

        snprintf(url, sizeof(url), "%s/_ingest/pipeline/tie", IndexCtx.es_url);
        r = web_put(url, pipeline_json, IndexCtx.es_insecure_ssl);
        LOG_INFOF("elastic.c", "Create pipeline <%d>", r->status_code);
        free_response(r);

        snprintf(url, sizeof(url), "%s/%s/_settings", IndexCtx.es_url, IndexCtx.es_index);
        r = web_put(url, user_settings ? user_settings : settings, IndexCtx.es_insecure_ssl);
        LOG_INFOF("elastic.c", "Update ES settings <%d>", r->status_code);
        if (r->status_code != 200) {
            print_error(r);
            LOG_FATAL("elastic.c", "Could not update user settings");
        }
        free_response(r);

        mappings_url(url, sizeof(url));

        r = web_put(url, user_mappings ? user_mappings : mappings_json, IndexCtx.es_insecure_ssl);
        LOG_INFOF("elastic.c", "Update ES mappings <%d>", r->status_code);
        if (r->status_code != 200) {
            print_error(r);
            LOG_FATAL("elastic.c", "Could not update user mappings");
        }
        free_response(r);

        snprintf(url, sizeof(url), "%s/%s/_open", IndexCtx.es_url, IndexCtx.es_index);
        r = web_post(url, "", IndexCtx.es_insecure_ssl);
        LOG_INFOF("elastic.c", "Open index <%d>", r->status_code);
        free_response(r);
    }
}

static void mappings_url(char *url, size_t url_size) {
    if (IS_LEGACY_VERSION(IndexCtx.es_version)) {
        snprintf(url, url_size, "%s/%s/_mappings/_doc?include_type_name=true",
                 IndexCtx.es_url, IndexCtx.es_index);
    } else {
        snprintf(url, url_size, "%s/%s/_mappings", IndexCtx.es_url, IndexCtx.es_index);
    }
}

// The _meta section of the mappings, or NULL. It is where the versions this index was filled from
// are kept: it lives and dies with the Elasticsearch index, so a deleted index means a full push.
static cJSON *elastic_get_meta() {
    char url[4096];
    snprintf(url, sizeof(url), "%s/%s/_mapping", IndexCtx.es_url, IndexCtx.es_index);

    response_t *r = web_get(url, 30, IndexCtx.es_insecure_ssl);

    cJSON *meta = NULL;

    if (r->status_code == 200) {
        char *tmp = malloc(r->size + 1);
        memcpy(tmp, r->body, r->size);
        *(tmp + r->size) = '\0';
        cJSON *json = cJSON_Parse(tmp);
        free(tmp);

        cJSON *index = cJSON_GetObjectItem(json, IndexCtx.es_index);
        cJSON *mappings = index == NULL ? NULL : cJSON_GetObjectItem(index, "mappings");
        cJSON *doc_type = mappings == NULL ? NULL : cJSON_GetObjectItem(mappings, "_doc");

        if (doc_type != NULL) {
            mappings = doc_type;
        }

        if (mappings != NULL) {
            meta = cJSON_DetachItemFromObject(mappings, "_meta");
        }

        cJSON_Delete(json);
    }

    free_response(r);

    return meta;
}

long long elastic_get_indexed_version(int index_id) {
    cJSON *meta = elastic_get_meta();

    if (meta == NULL) {
        return 0;
    }

    char index_id_str[32];
    snprintf(index_id_str, sizeof(index_id_str), "%d", index_id);

    const cJSON *versions = cJSON_GetObjectItem(meta, "sist2_versions");
    const cJSON *version = versions == NULL ? NULL : cJSON_GetObjectItem(versions, index_id_str);

    long long indexed_version = version == NULL ? 0 : (long long) version->valuedouble;

    cJSON_Delete(meta);

    return indexed_version;
}

void elastic_set_indexed_version(int index_id, long long version) {
    cJSON *meta = elastic_get_meta();

    if (meta == NULL) {
        meta = cJSON_CreateObject();
    }

    cJSON *versions = cJSON_GetObjectItem(meta, "sist2_versions");
    if (versions == NULL) {
        versions = cJSON_AddObjectToObject(meta, "sist2_versions");
    }

    char index_id_str[32];
    snprintf(index_id_str, sizeof(index_id_str), "%d", index_id);

    cJSON_DeleteItemFromObject(versions, index_id_str);
    cJSON_AddNumberToObject(versions, index_id_str, (double) version);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "_meta", meta);
    char *body_str = cJSON_PrintUnformatted(body);

    char url[4096];
    mappings_url(url, sizeof(url));

    response_t *r = web_put(url, body_str, IndexCtx.es_insecure_ssl);
    if (r->status_code != 200) {
        print_error(r);
        LOG_WARNING("elastic.c", "Could not save index version, the next run will push every document");
    }

    free_response(r);
    cJSON_free(body_str);
    cJSON_Delete(body);
}

cJSON *elastic_get_document(const char *id_str) {
    char url[4096];
    snprintf(url, sizeof(url), "%s/%s/_doc/%s", WebCtx.es_url, WebCtx.es_index, id_str);

    response_t *r = web_get(url, 3, WebCtx.es_insecure_ssl);
    cJSON *json = NULL;
    if (r->status_code == 200) {
        char *tmp = malloc(r->size + 1);
        memcpy(tmp, r->body, r->size);
        *(tmp + r->size) = '\0';
        json = cJSON_Parse(tmp);
        free(tmp);
    }
    free_response(r);
    return json;
}

char *elastic_get_status() {
    char url[4096];
    snprintf(url, sizeof(url),
             "%s/_cluster/state/metadata/%s?filter_path=metadata.indices.*.state", WebCtx.es_url, WebCtx.es_index);

    response_t *r = web_get(url, 30, IndexCtx.es_insecure_ssl);
    cJSON *json = NULL;
    char *status = malloc(128 * sizeof(char));
    status[0] = '\0';

    if (r->status_code == 200) {
        char *tmp = malloc(r->size + 1);
        memcpy(tmp, r->body, r->size);
        *(tmp + r->size) = '\0';
        json = cJSON_Parse(tmp);
        free(tmp);
        const cJSON *metadata = cJSON_GetObjectItem(json, "metadata");
        if (metadata != NULL) {
            const cJSON *indices = cJSON_GetObjectItem(metadata, "indices");
            const cJSON *index = cJSON_GetObjectItem(indices, WebCtx.es_index);
            const cJSON *state = cJSON_GetObjectItem(index, "state");
            strcpy(status, state->valuestring);
        }
    }
    free_response(r);
    cJSON_Delete(json);
    return status;
}