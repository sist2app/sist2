#include "sist.h"
#include "ctx.h"

#include <third-party/argparse/argparse.h>
#include <locale.h>

#include "cli.h"
#include "tpool.h"
#include "io/walk.h"
#include "index/elastic.h"
#include "web/serve.h"
#include "parsing/mime.h"
#include "parsing/parse.h"
#include "ignorelist.h"

#include <signal.h>
#include <pthread.h>

#include "src/database/database.h"


static const char *const usage[] = {
        "sist2 scan [OPTION]... PATH",
        "sist2 index [OPTION]... INDEX",
        "sist2 sqlite-index [OPTION]... INDEX",
        "sist2 web [OPTION]... INDEX...",
        NULL,
};


void database_scan_begin(scan_args_t *args) {
    index_descriptor_t *desc = &ScanCtx.index.desc;

    database_t *db = database_create(args->output, INDEX_DATABASE);

    if (args->incremental) {
        // Update existing descriptor
        database_open(db);
        index_descriptor_t *original_desc = database_read_index_descriptor(db);

        // copy original index id
        desc->id = original_desc->id;

        if (original_desc->version_major != VersionMajor) {
            LOG_FATALF("main.c", "Version mismatch! Index is %s but executable is %s", original_desc->version, Version);
        }

        strcpy(original_desc->root, desc->root);
        original_desc->root_len = desc->root_len;
        strcpy(original_desc->rewrite_url, desc->rewrite_url);
        strcpy(original_desc->name, desc->name);

        time(&original_desc->timestamp);

        database_write_index_descriptor(db, original_desc);
        free(original_desc);

        database_incremental_scan_begin(db);

    } else {
        // Create new descriptor

        time(&desc->timestamp);
        strcpy(desc->version, Version);
        desc->version_major = VersionMajor;
        desc->version_minor = VersionMinor;
        desc->version_patch = VersionPatch;

        // generate new index id based on timestamp
        desc->id = (int) ScanCtx.index.desc.timestamp;

        database_initialize(db);
        database_open(db);
        database_write_index_descriptor(db, desc);
    }

    database_increment_version(db);
    database_sync_mime_table(db);

    database_close(db, FALSE);
}

void log_callback(const char *filepath, int level, char *str) {
    if (level == LEVEL_FATAL) {
        sist_log(filepath, level, str);
        exit(-1);
    }

    if (LogCtx.verbose) {
        if (level == LEVEL_DEBUG) {
            if (LogCtx.very_verbose) {
                sist_log(filepath, level, str);
            }
        } else {
            sist_log(filepath, level, str);
        }
    }
}

void logf_callback(const char *filepath, int level, char *format, ...) {

    va_list args;

    va_start(args, format);
    if (level == LEVEL_FATAL) {
        vsist_logf(filepath, level, format, args);
        exit(-1);
    }

    if (LogCtx.verbose) {
        if (level == LEVEL_DEBUG) {
            if (LogCtx.very_verbose) {
                vsist_logf(filepath, level, format, args);
            }
        } else {
            vsist_logf(filepath, level, format, args);
        }
    }
    va_end(args);
}

void initialize_scan_context(scan_args_t *args) {

    ScanCtx.calculate_checksums = args->calculate_checksums;

    // Archive
    ScanCtx.arc_ctx.mode = args->archive_mode;
    ScanCtx.arc_ctx.log = log_callback;
    ScanCtx.arc_ctx.logf = logf_callback;
    ScanCtx.arc_ctx.parse = (parse_callback_t) parse;
    if (args->archive_passphrase != NULL) {
        strcpy(ScanCtx.arc_ctx.passphrase, args->archive_passphrase);
    } else {
        ScanCtx.arc_ctx.passphrase[0] = 0;
    }

    // Comic
    ScanCtx.comic_ctx.log = log_callback;
    ScanCtx.comic_ctx.logf = logf_callback;
    ScanCtx.comic_ctx.enable_tn = args->tn_count > 0;
    ScanCtx.comic_ctx.tn_size = args->tn_size;
    ScanCtx.comic_ctx.tn_qscale = args->tn_quality;
    ScanCtx.comic_ctx.cbr_mime = mime_get_mime_by_string("application/x-cbr");
    ScanCtx.comic_ctx.cbz_mime = mime_get_mime_by_string("application/x-cbz");

    // Ebook
    ScanCtx.ebook_ctx.content_size = args->content_size;
    ScanCtx.ebook_ctx.enable_tn = args->tn_count > 0;
    ScanCtx.ebook_ctx.tn_size = args->tn_size;
    if (args->ocr_ebooks) {
        ScanCtx.ebook_ctx.tesseract_lang = args->tesseract_lang;
        ScanCtx.ebook_ctx.tesseract_path = args->tesseract_path;
    }
    ScanCtx.ebook_ctx.log = log_callback;
    ScanCtx.ebook_ctx.logf = logf_callback;
    ScanCtx.ebook_ctx.fast_epub_parse = args->fast_epub;
    ScanCtx.ebook_ctx.tn_qscale = args->tn_quality;

    // Font
    ScanCtx.font_ctx.enable_tn = args->tn_count > 0;
    ScanCtx.font_ctx.log = log_callback;
    ScanCtx.font_ctx.logf = logf_callback;

    // Media
    ScanCtx.media_ctx.tn_qscale = args->tn_quality;
    ScanCtx.media_ctx.tn_size = args->tn_size;
    ScanCtx.media_ctx.tn_count = args->tn_count;
    ScanCtx.media_ctx.log = log_callback;
    ScanCtx.media_ctx.logf = logf_callback;
    ScanCtx.media_ctx.max_media_buffer = (long) args->max_memory_buffer_mib * 1024 * 1024;
    ScanCtx.media_ctx.read_subtitles = args->read_subtitles;
    ScanCtx.media_ctx.read_subtitles = args->tn_count;

    if (args->ocr_images) {
        ScanCtx.media_ctx.tesseract_lang = args->tesseract_lang;
        ScanCtx.media_ctx.tesseract_path = args->tesseract_path;
    }
    init_media();

    // OOXML
    ScanCtx.ooxml_ctx.enable_tn = args->tn_count > 0;
    ScanCtx.ooxml_ctx.content_size = args->content_size;
    ScanCtx.ooxml_ctx.log = log_callback;
    ScanCtx.ooxml_ctx.logf = logf_callback;

    // MOBI
    ScanCtx.mobi_ctx.content_size = args->content_size;
    ScanCtx.mobi_ctx.log = log_callback;
    ScanCtx.mobi_ctx.logf = logf_callback;
    ScanCtx.mobi_ctx.enable_tn = args->tn_count > 0;
    ScanCtx.mobi_ctx.tn_size = args->tn_size;
    ScanCtx.mobi_ctx.tn_qscale = args->tn_quality;

    // TEXT
    ScanCtx.text_ctx.content_size = args->content_size;
    ScanCtx.text_ctx.log = log_callback;
    ScanCtx.text_ctx.logf = logf_callback;

    // MSDOC
    ScanCtx.msdoc_ctx.content_size = args->content_size;
    ScanCtx.msdoc_ctx.log = log_callback;
    ScanCtx.msdoc_ctx.logf = logf_callback;
    ScanCtx.msdoc_ctx.msdoc_mime = mime_get_mime_by_string("application/msword");

    ScanCtx.threads = args->threads;
    ScanCtx.depth = args->depth;

    strncpy(ScanCtx.index.path, args->output, sizeof(ScanCtx.index.path));
    strncpy(ScanCtx.index.desc.name, args->name, sizeof(ScanCtx.index.desc.name));
    strncpy(ScanCtx.index.desc.root, args->path, sizeof(ScanCtx.index.desc.root));
    strncpy(ScanCtx.index.desc.rewrite_url, args->rewrite_url, sizeof(ScanCtx.index.desc.rewrite_url));
    ScanCtx.index.desc.root_len = (short) strlen(ScanCtx.index.desc.root);
    ScanCtx.fast = args->fast;

    // Raw
    ScanCtx.raw_ctx.tn_qscale = args->tn_quality;
    ScanCtx.raw_ctx.enable_tn = args->tn_count > 0;
    ScanCtx.raw_ctx.tn_size = args->tn_size;
    ScanCtx.raw_ctx.log = log_callback;
    ScanCtx.raw_ctx.logf = logf_callback;

    // Wpd
    ScanCtx.wpd_ctx.content_size = args->content_size;
    ScanCtx.wpd_ctx.log = log_callback;
    ScanCtx.wpd_ctx.logf = logf_callback;
    ScanCtx.wpd_ctx.wpd_mime = mime_get_mime_by_string("application/wordperfect");

    // Json
    ScanCtx.json_ctx.content_size = args->content_size;
    ScanCtx.json_ctx.log = log_callback;
    ScanCtx.json_ctx.logf = logf_callback;
    ScanCtx.json_ctx.json_mime = mime_get_mime_by_string("application/json");
    ScanCtx.json_ctx.ndjson_mime = mime_get_mime_by_string("application/ndjson");
}

void sist2_scan(scan_args_t *args) {
    initialize_scan_context(args);

    database_scan_begin(args);

    LOG_INFOF("main.c", "sist2 v%s", Version);

    ScanCtx.ignorelist = ignorelist_create();

    char ignore_filepath[PATH_MAX];
    sprintf(ignore_filepath, "%s.sist2ignore", args->path);

    ignorelist_load_ignore_file(ScanCtx.ignorelist, ignore_filepath);

    ScanCtx.pool = tpool_create(ScanCtx.threads, TRUE);
    tpool_start(ScanCtx.pool);

    if (args->list_path) {
        // Scan using file list
        int list_ret = iterate_file_list(args->list_file);
        if (list_ret != 0) {
            LOG_FATALF("main.c", "iterate_file_list() failed! (%d)", list_ret);
        }
    } else {
        // Scan directory recursively
        int walk_ret = walk_directory_tree(ScanCtx.index.desc.root);
        if (walk_ret == -1) {
            LOG_FATALF("main.c", "walk_directory_tree() failed! %s (%d)", strerror(errno), errno);
        }
    }

    tpool_wait(ScanCtx.pool);
    tpool_destroy(ScanCtx.pool);

    database_t *db = database_create(args->output, INDEX_DATABASE);
    database_open(db);

    if (args->incremental != FALSE) {
        database_incremental_scan_end(db);
    }

    database_generate_stats(db, args->treemap_threshold);
    database_close(db, args->optimize_database);
    ignorelist_destroy(ScanCtx.ignorelist);
}

void sist2_index(index_args_t *args) {
    IndexCtx.es_url = args->es_url;
    IndexCtx.es_index = args->es_index;
    IndexCtx.es_insecure_ssl = args->es_insecure_ssl;
    IndexCtx.batch_size = args->batch_size;
    IndexCtx.needs_es_connection = !args->print;

    if (IndexCtx.needs_es_connection) {
        elastic_init(args->force_reset, args->es_mappings, args->es_settings);
    }

    database_t *db = database_create(args->index_path, INDEX_DATABASE);
    database_open(db);
    index_descriptor_t *desc = database_read_index_descriptor(db);
    database_close(db, FALSE);

    LOG_DEBUGF("main.c", "Index version %s", desc->version);

    if (desc->version_major != VersionMajor) {
        LOG_FATALF("main.c", "Version mismatch! Index is %s but executable is %s", desc->version, Version);
    }

    IndexCtx.pool = tpool_create(args->threads, args->print == FALSE);
    tpool_start(IndexCtx.pool);

    int cnt = 0;

    db = database_create(args->index_path, INDEX_DATABASE);
    database_open(db);
    database_iterator_t *iterator = database_create_document_iterator(db);
    database_document_iter_foreach(json, iterator) {
        char sid[SIST_SID_LEN];
        int doc_id = cJSON_GetObjectItem(json, "_id")->valueint;
        cJSON_DeleteItemFromObject(json, "_id");
        format_sid(sid, desc->id, doc_id);

        if (args->print) {
            print_json(json, sid);
        } else {
            index_json(json, sid);
            cnt += 1;
        }
        cJSON_Delete(json);
    }

    free(iterator);

    if (!args->print) {
        char sid[SIST_SID_LEN];

        database_iterator_t *del_iter = database_create_delete_list_iterator(db);
        database_delete_list_iter_foreach(doc_id, del_iter) {
            format_sid(sid, desc->id, doc_id);
            delete_document(sid);
        }
        free(del_iter);
    }

    database_close(db, FALSE);

    tpool_wait(IndexCtx.pool);
    tpool_destroy(IndexCtx.pool);

    if (IndexCtx.needs_es_connection) {
        finish_indexer(desc->id);
    }
    free(desc);
}

void sist2_sqlite_index(sqlite_index_args_t *args) {
    database_t *db = database_create(args->index_path, INDEX_DATABASE);
    database_open(db);

    database_t *search_db = database_create(args->search_index_path, FTS_DATABASE);
    database_initialize(search_db);

    database_fts_attach(db, args->search_index_path);

    database_fts_index(db);
    database_fts_optimize(db);

    database_close(db, FALSE);
}

void sist2_web(web_args_t *args) {

    WebCtx.es_url = args->es_url;
    WebCtx.search_backend = args->search_backend;
    WebCtx.es_index = args->es_index;
    WebCtx.es_insecure_ssl = args->es_insecure_ssl;
    WebCtx.index_count = args->index_count;
    WebCtx.auth_user = args->auth_user;
    WebCtx.auth_pass = args->auth_pass;
    WebCtx.auth_enabled = args->auth_enabled;
    WebCtx.tag_auth_enabled = args->tag_auth_enabled;
    WebCtx.tagline = args->tagline;
    WebCtx.dev = args->dev;
    WebCtx.auth0_enabled = args->auth0_enabled;
    WebCtx.auth0_public_key = args->auth0_public_key;
    WebCtx.auth0_client_id = args->auth0_client_id;
    WebCtx.auth0_domain = args->auth0_domain;
    WebCtx.auth0_audience = args->auth0_audience;
    strcpy(WebCtx.lang, args->lang);

    if (args->search_backend == SQLITE_SEARCH_BACKEND) {
        WebCtx.search_db = database_create(args->search_index_path, FTS_DATABASE);
        database_open(WebCtx.search_db);
    }

    for (int i = 0; i < args->index_count; i++) {
        char *abs_path = abspath(args->indices[i]);

        strcpy(WebCtx.indices[i].path, abs_path);

        database_t *db = database_create(abs_path, INDEX_DATABASE);
        database_open(db);
        if (WebCtx.search_backend == SQLITE_SEARCH_BACKEND) {
            database_fts_attach(db, args->search_index_path);
            database_fts_sync_tags(db);
            database_fts_detach(db);
        }

        WebCtx.indices[i].db = db;

        index_descriptor_t *desc = database_read_index_descriptor(db);
        WebCtx.indices[i].desc = *desc;
        free(desc);

        LOG_INFOF("main.c", "Loaded index: [%s]", WebCtx.indices[i].desc.name);
        free(abs_path);
    }

    serve(args->listen_address);
}

/**
 * Callback to handle options such that
 *
 *   Unspecified              -> 0: Set to default value
 *   Specified "0"            -> -1: Disable the option (ex. don't generate thumbnails)
 *   Negative number          -> Raise error
 *   Specified a valid number -> Continue as normal
 */
int set_to_negative_if_value_is_zero(UNUSED(struct argparse *self), const struct argparse_option *option) {
    int specified_value = *(int *) option->value;

    if (specified_value == 0) {
        *((int *) option->data) = OPTION_VALUE_DISABLE;
    }

    if (specified_value < 0) {
        fprintf(stderr, "error: option `--%s` Value must be >= 0\n", option->long_name);
        exit(1);
    }

    return 0;
}

int main(int argc, const char *argv[]) {
    setlocale(LC_ALL, "");

    scan_args_t *scan_args = scan_args_create();
    index_args_t *index_args = index_args_create();
    web_args_t *web_args = web_args_create();
    sqlite_index_args_t *sqlite_index_args = sqlite_index_args_create();

    int arg_version = 0;

    char *common_es_url = NULL;
    int common_es_insecure_ssl = 0;
    char *common_es_index = NULL;
    char *common_script_path = NULL;
    int common_threads = 0;
    int common_optimize_database = 0;
    char *common_search_index = NULL;

    struct argparse_option options[] = {
            OPT_HELP(),

            OPT_BOOLEAN('v', "version", &arg_version, "Print version and exit."),
            OPT_BOOLEAN(0, "verbose", &LogCtx.verbose, "Turn on logging."),
            OPT_BOOLEAN(0, "very-verbose", &LogCtx.very_verbose, "Turn on debug messages."),
            OPT_BOOLEAN(0, "json-logs", &LogCtx.json_logs, "Output logs in JSON format."),

            OPT_GROUP("Scan options"),
            OPT_INTEGER('t', "threads", &common_threads, "Number of threads. DEFAULT: 1"),
            OPT_INTEGER('q', "thumbnail-quality", &scan_args->tn_quality,
                        "Thumbnail quality, on a scale of 0 to 100, 100 being the best. DEFAULT: 50",
                        set_to_negative_if_value_is_zero, (intptr_t) &scan_args->tn_quality),
            OPT_INTEGER(0, "thumbnail-size", &scan_args->tn_size,
                        "Thumbnail size, in pixels. DEFAULT: 552",
                        set_to_negative_if_value_is_zero, (intptr_t) &scan_args->tn_size),
            OPT_INTEGER(0, "thumbnail-count", &scan_args->tn_count,
                        "Number of thumbnails to generate. Set a value > 1 to create video previews, set to 0 to disable thumbnails. DEFAULT: 1",
                        set_to_negative_if_value_is_zero, (intptr_t) &scan_args->tn_count),
            OPT_INTEGER(0, "content-size", &scan_args->content_size,
                        "Number of bytes to be extracted from text documents. Set to 0 to disable. DEFAULT: 32768",
                        set_to_negative_if_value_is_zero, (intptr_t) &scan_args->content_size),
            OPT_STRING('o', "output", &scan_args->output, "Output index file path. DEFAULT: index.sist2"),
            OPT_BOOLEAN(0, "incremental", &scan_args->incremental,
                        "If the output file path exists, only scan new or modified files."),
            OPT_BOOLEAN(0, "optimize-index", &common_optimize_database,
                        "Defragment index file after scan to reduce its file size."),
            OPT_STRING(0, "rewrite-url", &scan_args->rewrite_url, "Serve files from this url instead of from disk."),
            OPT_STRING(0, "name", &scan_args->name, "Index display name. DEFAULT: index"),
            OPT_INTEGER(0, "depth", &scan_args->depth, "Scan up to DEPTH subdirectories deep. "
                                                       "Use 0 to only scan files in PATH. DEFAULT: -1"),
            OPT_STRING(0, "archive", &scan_args->archive, "Archive file mode (skip|list|shallow|recurse). "
                                                          "skip: don't scan, list: only save file names as text, "
                                                          "shallow: don't scan archives inside archives. DEFAULT: recurse"),
            OPT_STRING(0, "archive-passphrase", &scan_args->archive_passphrase,
                       "Passphrase for encrypted archive files"),

            OPT_STRING(0, "ocr-lang", &scan_args->tesseract_lang,
                       "Tesseract language (use 'tesseract --list-langs' to see "
                       "which are installed on your machine)"),
            OPT_BOOLEAN(0, "ocr-images", &scan_args->ocr_images, "Enable OCR'ing of image files."),
            OPT_BOOLEAN(0, "ocr-ebooks", &scan_args->ocr_ebooks, "Enable OCR'ing of ebook files."),
            OPT_STRING('e', "exclude", &scan_args->exclude_regex, "Files that match this regex will not be scanned."),
            OPT_BOOLEAN(0, "fast", &scan_args->fast, "Only index file names & mime type."),
            OPT_STRING(0, "treemap-threshold", &scan_args->treemap_threshold_str, "Relative size threshold for treemap "
                                                                                  "(see USAGE.md). DEFAULT: 0.0005"),
            OPT_INTEGER(0, "mem-buffer", &scan_args->max_memory_buffer_mib,
                        "Maximum memory buffer size per thread in MiB for files inside archives "
                        "(see USAGE.md). DEFAULT: 2000"),
            OPT_BOOLEAN(0, "read-subtitles", &scan_args->read_subtitles, "Read subtitles from media files."),
            OPT_BOOLEAN(0, "fast-epub", &scan_args->fast_epub,
                        "Faster but less accurate EPUB parsing (no thumbnails, metadata)."),
            OPT_BOOLEAN(0, "checksums", &scan_args->calculate_checksums, "Calculate file checksums when scanning."),
            OPT_STRING(0, "list-file", &scan_args->list_path, "Specify a list of newline-delimited paths to be scanned"
                                                              " instead of normal directory traversal. Use '-' to read"
                                                              " from stdin."),

            OPT_GROUP("Index options"),
            OPT_INTEGER('t', "threads", &common_threads, "Number of threads. DEFAULT: 1"),
            OPT_STRING(0, "es-url", &common_es_url, "Elasticsearch url with port. DEFAULT: http://localhost:9200"),
            OPT_BOOLEAN(0, "es-insecure-ssl", &common_es_insecure_ssl,
                        "Do not verify SSL connections to Elasticsearch."),
            OPT_STRING(0, "es-index", &common_es_index, "Elasticsearch index name. DEFAULT: sist2"),
            OPT_BOOLEAN('p', "print", &index_args->print,
                        "Print JSON documents to stdout instead of indexing to elasticsearch."),
            OPT_BOOLEAN(0, "incremental-index", &index_args->incremental,
                        "Conduct incremental indexing. Assumes that the old index is already ingested in Elasticsearch."),
            OPT_STRING(0, "script-file", &common_script_path, "Path to user script."),
            OPT_STRING(0, "mappings-file", &index_args->es_mappings_path, "Path to Elasticsearch mappings."),
            OPT_STRING(0, "settings-file", &index_args->es_settings_path, "Path to Elasticsearch settings."),
            OPT_INTEGER(0, "batch-size", &index_args->batch_size, "Index batch size. DEFAULT: 70"),
            OPT_BOOLEAN('f', "force-reset", &index_args->force_reset, "Reset Elasticsearch mappings and settings."),

            OPT_GROUP("sqlite-index options"),
            OPT_STRING(0, "search-index", &common_search_index,
                       "Path to search index. Will be created if it does not exist yet."),

            OPT_GROUP("Web options"),
            OPT_STRING(0, "es-url", &common_es_url, "Elasticsearch url. DEFAULT: http://localhost:9200"),
            OPT_BOOLEAN(0, "es-insecure-ssl", &common_es_insecure_ssl,
                        "Do not verify SSL connections to Elasticsearch."),
            OPT_STRING(0, "search-index", &common_search_index, "Path to SQLite search index."),
            OPT_STRING(0, "es-index", &common_es_index, "Elasticsearch index name. DEFAULT: sist2"),
            OPT_STRING(0, "bind", &web_args->listen_address,
                       "Listen for connections on this address. DEFAULT: localhost:4090"),
            OPT_STRING(0, "auth", &web_args->credentials, "Basic auth in user:password format"),
            OPT_STRING(0, "auth0-audience", &web_args->auth0_audience, "API audience/identifier"),
            OPT_STRING(0, "auth0-domain", &web_args->auth0_domain, "Application domain"),
            OPT_STRING(0, "auth0-client-id", &web_args->auth0_client_id, "Application client ID"),
            OPT_STRING(0, "auth0-public-key-file", &web_args->auth0_public_key_path,
                       "Path to Auth0 public key file extracted from <domain>/pem"),
            OPT_STRING(0, "tag-auth", &web_args->tag_credentials, "Basic auth in user:password format for tagging"),
            OPT_STRING(0, "tagline", &web_args->tagline, "Tagline in navbar"),
            OPT_BOOLEAN(0, "dev", &web_args->dev, "Serve html & js files from disk (for development)"),
            OPT_STRING(0, "lang", &web_args->lang, "Default UI language. Can be changed by the user"),

            OPT_END(),
    };

    struct argparse argparse = {};
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(
            &argparse,
            "\nLightning-fast file system indexer and search tool.",
            "\nMade by simon987 <me@simon987.net>. Released under GPL-3.0"
    );
    argc = argparse_parse(&argparse, argc, argv);

    if (arg_version) {
        printf("%s", Version);
        goto end;
    }

    if (LogCtx.very_verbose != 0) {
        LogCtx.verbose = 1;
    }

    web_args->es_url = common_es_url;
    index_args->es_url = common_es_url;

    web_args->es_index = common_es_index;
    index_args->es_index = common_es_index;

    web_args->es_insecure_ssl = common_es_insecure_ssl;
    index_args->es_insecure_ssl = common_es_insecure_ssl;

    index_args->script_path = common_script_path;
    index_args->threads = common_threads;
    scan_args->threads = common_threads;

    scan_args->optimize_database = common_optimize_database;

    sqlite_index_args->search_index_path = common_search_index;
    web_args->search_index_path = common_search_index;

    if (argc == 0) {
        argparse_usage(&argparse);
        goto end;
    } else if (strcmp(argv[0], "scan") == 0) {

        int err = scan_args_validate(scan_args, argc, argv);
        if (err != 0) {
            goto end;
        }
        sist2_scan(scan_args);

    } else if (strcmp(argv[0], "index") == 0) {

        int err = index_args_validate(index_args, argc, argv);
        if (err != 0) {
            goto end;
        }
        sist2_index(index_args);

    } else if (strcmp(argv[0], "sqlite-index") == 0) {

        int err = sqlite_index_args_validate(sqlite_index_args, argc, argv);
        if (err != 0) {
            goto end;
        }
        sist2_sqlite_index(sqlite_index_args);

    } else if (strcmp(argv[0], "web") == 0) {

        int err = web_args_validate(web_args, argc, argv);
        if (err != 0) {
            goto end;
        }
        sist2_web(web_args);

    } else {
        argparse_usage(&argparse);
        LOG_FATALF("main.c", "Invalid command: '%s'\n", argv[0]);
    }
    printf("\n");

    end:
    scan_args_destroy(scan_args);
    index_args_destroy(index_args);
    web_args_destroy(web_args);
    sqlite_index_args_destroy(sqlite_index_args);

    return 0;
}
