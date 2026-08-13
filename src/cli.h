#ifndef SIST2_CLI_H
#define SIST2_CLI_H

#include "sist.h"

#include "libscan/arc/arc.h"

#define OPTION_VALUE_DISABLE (-1)
#define OPTION_VALUE_UNSPECIFIED (0)

typedef struct scan_args {
    int tn_quality;
    int tn_size;
    int content_size;
    int threads;
    int incremental;
    int optimize_database;
    char *output;
    char *rewrite_url;
    char *name;
    int depth;
    char *path;
    char *archive;
    archive_mode_t archive_mode;
    char *archive_passphrase;
    char *tesseract_lang;
    const char *tesseract_path;
    int ocr_images;
    int ocr_ebooks;
    char *exclude_regex;
    int fast;
    const char* treemap_threshold_str;
    double treemap_threshold;
    int max_memory_buffer_mib;
    int read_subtitles;
    /** Number of thumbnails to generate */
    int tn_count;
    int fast_epub;
    int calculate_checksums;
    char *list_path;
    FILE *list_file;
    /** Set on the child processes the scan master spawns; not meant to be passed by hand */
    int worker;
    /** Seconds a worker may spend on one file before it is killed and restarted, 0 to disable */
    int job_timeout;
} scan_args_t;

scan_args_t *scan_args_create();

void scan_args_destroy(scan_args_t *args);

int scan_args_validate(scan_args_t *args, int argc, const char **argv);

typedef struct index_args {
    char *es_url;
    char *es_index;
    int es_insecure_ssl;
    char *index_path;
    const char *script_path;
    char *script;
    const char *es_settings_path;
    char *es_settings;
    const char *es_mappings_path;
    char *es_mappings;
    int print;
    int batch_size;
    int async_script;
    int force_reset;
    int threads;
    int incremental;
} index_args_t;

typedef struct {
    char *index_path;
    char *search_index_path;
    int rebuild;
    int optimize;
} sqlite_index_args_t;

typedef enum {
    ES_SEARCH_BACKEND,
    SQLITE_SEARCH_BACKEND,
} search_backend_t;

typedef struct web_args {
    char *es_url;
    char *es_index;
    int es_insecure_ssl;
    char *search_index_path;
    char *listen_address;
    char *credentials;
    char *tag_credentials;
    char *tagline;
    char *lang;
    char auth_user[256];
    char auth_pass[256];
    int auth0_enabled;
    char *auth0_audience;
    char *auth0_domain;
    char *auth0_client_id;
    char *auth0_public_key_path;
    char *auth0_public_key;
    int auth_enabled;
    int tag_auth_enabled;
    int index_count;
    int dev;
    const char **indices;
    search_backend_t search_backend;
} web_args_t;

index_args_t *index_args_create();

sqlite_index_args_t *sqlite_index_args_create();

void index_args_destroy(index_args_t *args);

web_args_t *web_args_create();

void web_args_destroy(web_args_t *args);

int index_args_validate(index_args_t *args, int argc, const char **argv);

int sqlite_index_args_validate(sqlite_index_args_t *args, int argc, const char **argv);

int web_args_validate(web_args_t *args, int argc, const char **argv);

void sqlite_index_args_destroy(sqlite_index_args_t *args);


#endif
