#ifndef SIST2_DATABASE_H
#define SIST2_DATABASE_H

#include <sqlite3.h>
#include <cjson/cJSON.h>
#include "src/sist.h"
#include "src/index/elastic.h"

typedef struct index_descriptor index_descriptor_t;

extern const char *IpcDatabaseSchema;
extern const char *IndexDatabaseSchema;
extern const char *FtsDatabaseSchema;

typedef enum {
    INDEX_DATABASE,
    IPC_CONSUMER_DATABASE,
    IPC_PRODUCER_DATABASE,
    FTS_DATABASE
} database_type_t;

typedef enum {
    DATABASE_STAT_INVALID,
    DATABASE_STAT_TREEMAP,
    DATABASE_STAT_MIME_AGG,
    DATABASE_STAT_SIZE_AGG,
    DATABASE_STAT_DATE_AGG,
} database_stat_type_d;

typedef enum {
    JOB_UNDEFINED,
    JOB_BULK_LINE,
    JOB_PARSE_JOB
} job_type_t;

typedef enum {
    FTS_SORT_INVALID,
    FTS_SORT_SCORE,
    FTS_SORT_SIZE,
    FTS_SORT_MTIME,
    FTS_SORT_RANDOM,
    FTS_SORT_NAME,
    FTS_SORT_ID,
    FTS_SORT_EMBEDDING
} fts_sort_t;

typedef struct {
    job_type_t type;
    union {
        parse_job_t *parse_job;
        es_bulk_line_t *bulk_line;
    };
} job_t;

typedef struct {
    int job_count;
    int no_more_jobs;
    int completed_job_count;

    pthread_mutex_t mutex;
    pthread_mutex_t db_mutex;
    pthread_mutex_t index_db_mutex;
    pthread_cond_t has_work_cond;
    char current_job[MAX_THREADS][PATH_MAX * 2];
} database_ipc_ctx_t;

#define SET_CURRENT_JOB(ctx, job) (strcpy((ctx)->current_job[ProcData.thread_id], job))

typedef struct {
    double date_min;
    double date_max;
} database_summary_stats_t;

typedef struct database {
    char filename[PATH_MAX];
    database_type_t type;
    sqlite3 *db;

    // Prepared statements
    sqlite3_stmt *select_thumbnail_stmt;
    sqlite3_stmt *treemap_merge_up_update_stmt;
    sqlite3_stmt *treemap_merge_up_delete_stmt;

    sqlite3_stmt *mark_document_stmt;
    sqlite3_stmt *write_document_stmt;
    sqlite3_stmt *write_thumbnail_stmt;
    sqlite3_stmt *get_document;
    sqlite3_stmt *get_models;
    sqlite3_stmt *get_embedding;

    sqlite3_stmt *delete_tag_stmt;
    sqlite3_stmt *write_tag_stmt;

    sqlite3_stmt *insert_parse_job_stmt;
    sqlite3_stmt *insert_index_job_stmt;
    sqlite3_stmt *pop_parse_job_stmt;
    sqlite3_stmt *pop_index_job_stmt;

    sqlite3_stmt *fts_search_paths;
    sqlite3_stmt *fts_search_paths_w_prefix;
    sqlite3_stmt *fts_suggest_paths;
    sqlite3_stmt *fts_date_range;
    sqlite3_stmt *fts_get_mimetypes;
    sqlite3_stmt *fts_get_document;
    sqlite3_stmt *fts_suggest_tag;
    sqlite3_stmt *fts_get_tags;
    sqlite3_stmt *fts_write_tag_stmt;
    sqlite3_stmt *fts_model_size;

    char **tag_array;

    database_ipc_ctx_t *ipc_ctx;
} database_t;

typedef struct {
    database_t *db;
    sqlite3_stmt *stmt;
} database_iterator_t;

typedef struct {
    const char *path;
    const char *parent;
    long size;
} treemap_row_t;


database_t *database_create(const char *filename, database_type_t type);

void database_initialize(database_t *db);

void database_open(database_t *db);

void database_close(database_t *, int optimize);

void database_increment_version(database_t *db);

void database_write_thumbnail(database_t *db, int doc_id, int num, void *data, size_t data_size);

void *database_read_thumbnail(database_t *db, int doc_id, int num, size_t *return_value_len);

void database_write_index_descriptor(database_t *db, index_descriptor_t *desc);

index_descriptor_t *database_read_index_descriptor(database_t *db);

int database_write_document(database_t *db, document_t *doc, const char *json_data);

database_iterator_t *database_create_document_iterator(database_t *db);

void emb_to_json_func(sqlite3_context *ctx, int argc, sqlite3_value **argv);

cJSON *database_document_iter(database_iterator_t *);

#define database_document_iter_foreach(element, iter) \
    for (cJSON *(element) = database_document_iter(iter); (element) != NULL; (element) = database_document_iter(iter))

database_iterator_t *database_create_delete_list_iterator(database_t *db);

int database_delete_list_iter(database_iterator_t *iter);

#define database_delete_list_iter_foreach(element, iter) \
    for (int (element) = database_delete_list_iter(iter); (element) != 0; (element) = database_delete_list_iter(iter))


cJSON *database_incremental_scan_begin(database_t *db);

cJSON *database_incremental_scan_end(database_t *db);

int database_mark_document(database_t *db, const char *id, int mtime);

database_iterator_t *database_create_treemap_iterator(database_t *db, long threshold);

treemap_row_t database_treemap_iter(database_iterator_t *iter);

#define database_treemap_iter_foreach(element, iter) \
    for (treemap_row_t element = database_treemap_iter(iter); (element).path != NULL; (element) = database_treemap_iter(iter))


void database_generate_stats(database_t *db, double treemap_threshold);

database_stat_type_d database_get_stat_type_by_mnemonic(const char *name);

job_t *database_get_work(database_t *db, job_type_t job_type);

void database_add_work(database_t *db, job_t *job);

cJSON *database_get_stats(database_t *db, database_stat_type_d type);

#define CRASH_IF_STMT_FAIL(x) do { \
        int return_value = x;                \
        if (return_value != SQLITE_DONE && return_value != SQLITE_ROW) {     \
            LOG_FATALF("database.c", "Sqlite error @ %s:%d : (%d) %s", __BASE_FILE__, __LINE__, return_value, sqlite3_errmsg(db->db)); \
        }                           \
    } while (0)

#define CRASH_IF_NOT_SQLITE_OK(x) do { \
        int return_value = x;                \
        if (return_value != SQLITE_OK) {     \
            LOG_FATALF("database.c", "Sqlite error @ %s:%d : (%d) %s", __BASE_FILE__, __LINE__, return_value, sqlite3_errmsg(db->db)); \
        }                           \
    } while (0)

void database_fts_attach(database_t *db, const char *fts_database_path);

void database_fts_index(database_t *db);

void database_fts_optimize(database_t *db);

cJSON *database_fts_get_paths(database_t *db, int index_id, int depth_min, int depth_max, const char *prefix,
                              int suggest);

cJSON *database_fts_get_mimetypes(database_t *db);

database_summary_stats_t database_fts_get_date_range(database_t *db);

cJSON *database_fts_search(database_t *db, const char *query, const char *path, long size_min,
                           long size_max, long date_min, long date_max, int page_size,
                           int *index_ids, char **mime_types, char **tags, int sort_asc,
                           fts_sort_t sort, int seed, char **after, int fetch_aggregations,
                           int highlight, int highlight_context_size, int model,
                           const float *embedding, int embedding_size);

void database_write_tag(database_t *db, long sid, char *tag);

void database_fts_write_tag(database_t *db, long sid, char *tag);

void database_delete_tag(database_t *db, long sid, char *tag);

void database_fts_detach(database_t *db);

cJSON *database_fts_get_document(database_t *db, long sid);

database_summary_stats_t database_fts_sync_tags(database_t *db);

cJSON *database_fts_suggest_tag(database_t *db, char *prefix);

cJSON *database_fts_get_tags(database_t *db);

cJSON *database_get_document(database_t *db, int doc_id);

void cosine_sim_func(sqlite3_context *ctx, int argc, sqlite3_value **argv);

cJSON *database_get_models(database_t *db);

int database_fts_get_model_size(database_t *db, int model_id);

cJSON *database_get_embedding(database_t *db, int doc_id, int model_id);

void database_sync_mime_table(database_t *db);

#endif