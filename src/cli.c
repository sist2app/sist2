#include "cli.h"
#include "ctx.h"
#include <tesseract/capi.h>

#define DEFAULT_OUTPUT "index.sist2"
#define DEFAULT_NAME "index"
#define DEFAULT_CONTENT_SIZE 32768
#define DEFAULT_QUALITY 50
#define DEFAULT_THUMBNAIL_SIZE 552
#define DEFAULT_THUMBNAIL_COUNT 1
#define DEFAULT_REWRITE_URL ""

#define DEFAULT_ES_URL "http://localhost:9200"
#define DEFAULT_ES_INDEX "sist2"
#define DEFAULT_BATCH_SIZE 70
#define DEFAULT_TAGLINE "Lightning-fast file system indexer and search tool"
#define DEFAULT_LANG "en"

#define DEFAULT_LISTEN_ADDRESS "localhost:4090"
#define DEFAULT_TREEMAP_THRESHOLD 0.0005

#define DEFAULT_MAX_MEM_BUFFER 2000

const char *TESS_DATAPATHS[] = {
        "/usr/share/tessdata/",
        "/usr/share/tesseract-ocr/tessdata/",
        "/usr/share/tesseract-ocr/4.00/tessdata/",
        "/usr/share/tesseract-ocr/5/tessdata/",
        "./",
        NULL
};


scan_args_t *scan_args_create() {
    scan_args_t *args = calloc(sizeof(scan_args_t), 1);

    args->depth = -1;

    return args;
}

void scan_args_destroy(scan_args_t *args) {
    if (args->name != NULL) {
        free(args->name);
    }
    if (args->path != NULL) {
        free(args->path);
    }
    if (args->output != NULL) {
        free(args->output);
    }
    free(args);
}

void index_args_destroy(index_args_t *args) {
    if (args->es_mappings_path) {
        free(args->es_mappings);
    }
    if (args->es_settings_path) {
        free(args->es_settings);
    }

    if (args->index_path != NULL) {
        free(args->index_path);
    }
    free(args);
}

void web_args_destroy(web_args_t *args) {
    free(args);
}

void sqlite_index_args_destroy(sqlite_index_args_t *args) {
    free(args->index_path);
    free(args);
}

char *add_trailing_slash(char *abs_path) {
    if (strcmp(abs_path, "/") == 0) {
        // Special case: don't add trailing slash for "/"
        return abs_path;
    }

    char *new_abs_path = realloc(abs_path, strlen(abs_path) + 2);
    if (new_abs_path == NULL) {
        LOG_FATALF("cli.c", "FIXME: realloc() failed for abs_path=%s", abs_path);
    }
    strcat(new_abs_path, "/");

    return new_abs_path;
}

int scan_args_validate(scan_args_t *args, int argc, const char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Required positional argument: PATH.\n");
        return 1;
    }

    char *abs_path = abspath(argv[1]);
    if (abs_path == NULL) {
        LOG_FATALF("cli.c", "Invalid PATH argument. File not found: %s", argv[1]);
    }

    args->path = add_trailing_slash(abs_path);

    if (args->tn_quality == OPTION_VALUE_UNSPECIFIED) {
        args->tn_quality = DEFAULT_QUALITY;
    } else if (args->tn_quality < 0 || args->tn_quality > 100) {
        fprintf(stderr, "Invalid value for --thumbnail-quality argument: %d. Must be within [0, 100].\n",
                args->tn_quality);
        return 1;
    }

    if (args->tn_size == OPTION_VALUE_UNSPECIFIED) {
        args->tn_size = DEFAULT_THUMBNAIL_SIZE;
    } else if (args->tn_size < 32) {
        printf("Invalid value --thumbnail-size argument: %d. Must be >= 32 pixels.\n", args->tn_size);
        return 1;
    }

    if (args->tn_count == OPTION_VALUE_UNSPECIFIED) {
        args->tn_count = DEFAULT_THUMBNAIL_COUNT;
    } else if (args->tn_count == OPTION_VALUE_DISABLE) {
        args->tn_count = 0;
    } else if (args->tn_count > 1000) {
        printf("Invalid value --thumbnail-count argument: %d. Must be <= 1000.\n", args->tn_size);
        return 1;
    }

    if (args->content_size == OPTION_VALUE_UNSPECIFIED) {
        args->content_size = DEFAULT_CONTENT_SIZE;
    }

    if (args->threads == 0) {
        args->threads = 1;
    } else if (args->threads < 0 || args->threads > 256) {
        fprintf(stderr, "Invalid value for --threads: %d. Must be a positive number <= 256\n", args->threads);
        return 1;
    }

    if (args->output == OPTION_VALUE_UNSPECIFIED) {
        args->output = malloc(strlen(DEFAULT_OUTPUT) + 1);
        strcpy(args->output, DEFAULT_OUTPUT);
    } else {
        args->output = expandpath(args->output);
    }

    char *abs_output = abspath(args->output);
    if (args->incremental && abs_output == NULL) {
        LOG_WARNINGF("main.c",
                     "Could not open original index for incremental scan: %s. Will not perform incremental scan.",
                     args->output);
        args->incremental = FALSE;
    } else if (!args->incremental && abs_output != NULL) {
        LOG_FATALF("main.c",
                   "Index already exists: %s. If you wish to perform incremental scan, you must specify --incremental",
                   abs_output);
    }
    free(abs_output);

    if (args->depth <= 0) {
        args->depth = 2147483647;
    } else {
        args->depth += 1;
    }

    if (args->name == OPTION_VALUE_UNSPECIFIED) {
        args->name = malloc(strlen(DEFAULT_NAME) + 1);
        strcpy(args->name, DEFAULT_NAME);
    } else {
        char *tmp = malloc(strlen(args->name) + 1);
        strcpy(tmp, args->name);
        args->name = tmp;
    }

    if (args->rewrite_url == OPTION_VALUE_UNSPECIFIED) {
        args->rewrite_url = DEFAULT_REWRITE_URL;
    }

    if (args->archive == OPTION_VALUE_UNSPECIFIED || strcmp(args->archive, "recurse") == 0) {
        args->archive_mode = ARC_MODE_RECURSE;
    } else if (strcmp(args->archive, "list") == 0) {
        args->archive_mode = ARC_MODE_LIST;
    } else if (strcmp(args->archive, "shallow") == 0) {
        args->archive_mode = ARC_MODE_SHALLOW;
    } else if (strcmp(args->archive, "skip") == 0) {
        args->archive_mode = ARC_MODE_SKIP;
    } else {
        fprintf(stderr, "Archive mode must be one of (skip, list, shallow, recurse), got '%s'", args->archive);
        return 1;
    }

    if (args->ocr_images && args->tesseract_lang == OPTION_VALUE_UNSPECIFIED) {
        fprintf(stderr, "You must specify --ocr-lang <LANG> to use --ocr-images");
        return 1;
    }

    if (args->ocr_ebooks && args->tesseract_lang == OPTION_VALUE_UNSPECIFIED) {
        fprintf(stderr, "You must specify --ocr-lang <LANG> to use --ocr-ebooks");
        return 1;
    }

    if (args->tesseract_lang != OPTION_VALUE_UNSPECIFIED) {

        if (!args->ocr_ebooks && !args->ocr_images) {
            fprintf(stderr, "You must specify at least one of --ocr-ebooks, --ocr-images");
            return 1;
        }

        TessBaseAPI *api = TessBaseAPICreate();

        const char *trained_data_path = NULL;
        char *lang = malloc(strlen(args->tesseract_lang) + 1);
        strcpy(lang, args->tesseract_lang);

        lang = strtok(lang, "+");

        while (lang != NULL) {
            char filename[128];
            sprintf(filename, "%s.traineddata", lang);

            const char *path = find_file_in_paths(TESS_DATAPATHS, filename);
            if (path == NULL) {
                LOG_FATALF("cli.c", "Could not find tesseract language file: %s!", filename);
            }
            if (trained_data_path != NULL && path != trained_data_path) {
                LOG_FATAL("cli.c", "When specifying more than one tesseract language, all the traineddata "
                                   "files must be in the same folder");
            }
            trained_data_path = path;

            lang = strtok(NULL, "+");
        }
        free(lang);

        int ret = TessBaseAPIInit3(api, trained_data_path, args->tesseract_lang);
        if (ret != 0) {
            fprintf(stderr, "Could not initialize tesseract with lang '%s'\n", args->tesseract_lang);
            return 1;
        }
        TessBaseAPIEnd(api);
        TessBaseAPIDelete(api);

        args->tesseract_path = trained_data_path;
    }

    if (args->exclude_regex != OPTION_VALUE_UNSPECIFIED) {
        const char *error;
        int error_offset;

        pcre *re = pcre_compile(args->exclude_regex, 0, &error, &error_offset, 0);
        if (error != NULL) {
            LOG_FATALF("cli.c", "pcre_compile returned error: %s (offset:%d)", error, error_offset);
        }

        pcre_extra *re_extra = pcre_study(re, 0, &error);
        if (error != NULL) {
            LOG_FATALF("cli.c", "pcre_study returned error: %s", error);
        }

        ScanCtx.exclude = re;
        ScanCtx.exclude_extra = re_extra;
    } else {
        ScanCtx.exclude = NULL;
    }

    if (args->treemap_threshold_str == OPTION_VALUE_UNSPECIFIED) {
        args->treemap_threshold = DEFAULT_TREEMAP_THRESHOLD;
    } else {
        args->treemap_threshold = atof(args->treemap_threshold_str);
    }

    if (args->max_memory_buffer_mib == OPTION_VALUE_UNSPECIFIED) {
        args->max_memory_buffer_mib = DEFAULT_MAX_MEM_BUFFER;
    }

    if (args->list_path != OPTION_VALUE_UNSPECIFIED) {
        if (strcmp(args->list_path, "-") == 0) {
            args->list_file = stdin;
            LOG_DEBUG("cli.c", "Using stdin as list file");
        } else {
            args->list_file = fopen(args->list_path, "r");

            if (args->list_file == NULL) {
                LOG_FATALF("main.c", "List file could not be opened: %s (%s)", args->list_path, errno);
            }
        }
    }

    LOG_DEBUGF("cli.c", "arg tn_quality=%f", args->tn_quality);
    LOG_DEBUGF("cli.c", "arg tn_size=%d", args->tn_size);
    LOG_DEBUGF("cli.c", "arg tn_count=%d", args->tn_count);
    LOG_DEBUGF("cli.c", "arg content_size=%d", args->content_size);
    LOG_DEBUGF("cli.c", "arg threads=%d", args->threads);
    LOG_DEBUGF("cli.c", "arg incremental=%d", args->incremental);
    LOG_DEBUGF("cli.c", "arg output=%s", args->output);
    LOG_DEBUGF("cli.c", "arg rewrite_url=%s", args->rewrite_url);
    LOG_DEBUGF("cli.c", "arg name=%s", args->name);
    LOG_DEBUGF("cli.c", "arg depth=%d", args->depth);
    LOG_DEBUGF("cli.c", "arg path=%s", args->path);
    LOG_DEBUGF("cli.c", "arg archive=%s", args->archive);
    LOG_DEBUGF("cli.c", "arg archive_passphrase=%s", args->archive_passphrase);
    LOG_DEBUGF("cli.c", "arg tesseract_lang=%s", args->tesseract_lang);
    LOG_DEBUGF("cli.c", "arg tesseract_path=%s", args->tesseract_path);
    LOG_DEBUGF("cli.c", "arg exclude=%s", args->exclude_regex);
    LOG_DEBUGF("cli.c", "arg fast=%d", args->fast);
    LOG_DEBUGF("cli.c", "arg fast_epub=%d", args->fast_epub);
    LOG_DEBUGF("cli.c", "arg treemap_threshold=%f", args->treemap_threshold);
    LOG_DEBUGF("cli.c", "arg max_memory_buffer_mib=%d", args->max_memory_buffer_mib);
    LOG_DEBUGF("cli.c", "arg list_path=%s", args->list_path);

    return 0;
}

int load_external_file(const char *file_path, char **dst) {
    struct stat info;
    int res = stat(file_path, &info);

    if (res == -1) {
        LOG_ERRORF("cli.c", "Error opening file '%s': %s\n", file_path, strerror(errno));
        return 1;
    }

    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        LOG_ERRORF("cli.c", "Error opening file '%s': %s\n", file_path, strerror(errno));
        return 1;
    }

    *dst = malloc(info.st_size + 1);
    res = read(fd, *dst, info.st_size);
    if (res < 0) {
        LOG_ERRORF("cli.c", "Error reading file '%s': %s\n", file_path, strerror(errno));
        return 1;
    }

    *(*dst + info.st_size) = '\0';
    close(fd);

    return 0;
}

int index_args_validate(index_args_t *args, int argc, const char **argv) {

    LogCtx.verbose = 1;

    if (argc < 2) {
        fprintf(stderr, "Required positional argument: PATH.\n");
        return 1;
    }

    if (args->threads == 0) {
        args->threads = 1;
    } else if (args->threads < 0) {
        fprintf(stderr, "Invalid threads: %d\n", args->threads);
        return 1;
    }

    char *index_path = abspath(argv[1]);
    if (index_path == NULL) {
        LOG_FATALF("cli.c", "Invalid PATH argument. File not found: %s", argv[1]);
    } else {
        args->index_path = index_path;
    }

    if (args->es_url == NULL) {
        args->es_url = DEFAULT_ES_URL;
    }

    if (args->es_index == NULL) {
        args->es_index = DEFAULT_ES_INDEX;
    }

    if (args->script_path != NULL) {
        if (load_external_file(args->script_path, &args->script) != 0) {
            return 1;
        }
    }

    if (args->es_settings_path != NULL) {
        if (load_external_file(args->es_settings_path, &args->es_settings) != 0) {
            return 1;
        }
    }

    if (args->es_mappings_path != NULL) {
        if (load_external_file(args->es_mappings_path, &args->es_mappings) != 0) {
            return 1;
        }
    }

    if (args->batch_size == 0) {
        args->batch_size = DEFAULT_BATCH_SIZE;
    }

    LOG_DEBUGF("cli.c", "arg es_url=%s", args->es_url);
    LOG_DEBUGF("cli.c", "arg es_index=%s", args->es_index);
    LOG_DEBUGF("cli.c", "arg es_insecure_ssl=%d", args->es_insecure_ssl);
    LOG_DEBUGF("cli.c", "arg index_path=%s", args->index_path);
    LOG_DEBUGF("cli.c", "arg script_path=%s", args->script_path);
    LOG_DEBUGF("cli.c", "arg async_script=%d", args->async_script);

    if (args->script) {
        char log_buf[5000];

        strncpy(log_buf, args->script, sizeof(log_buf));
        *(log_buf + sizeof(log_buf) - 1) = '\0';
        LOG_DEBUGF("cli.c", "arg script=%s", log_buf);
    }

    LOG_DEBUGF("cli.c", "arg print=%d", args->print);
    LOG_DEBUGF("cli.c", "arg es_mappings_path=%s", args->es_mappings_path);
    LOG_DEBUGF("cli.c", "arg es_mappings=%s", args->es_mappings);
    LOG_DEBUGF("cli.c", "arg es_settings_path=%s", args->es_settings_path);
    LOG_DEBUGF("cli.c", "arg es_settings=%s", args->es_settings);
    LOG_DEBUGF("cli.c", "arg batch_size=%d", args->batch_size);
    LOG_DEBUGF("cli.c", "arg force_reset=%d", args->force_reset);

    return 0;
}

int sqlite_index_args_validate(sqlite_index_args_t *args, int argc, const char **argv) {

    LogCtx.verbose = 1;

    if (argc < 2) {
        fprintf(stderr, "Required positional argument: PATH.\n");
        return 1;
    }

    char *index_path = abspath(argv[1]);
    if (index_path == NULL) {
        LOG_FATALF("cli.c", "Invalid PATH argument. File not found: %s", argv[1]);
    } else {
        args->index_path = index_path;
    }

    if (args->search_index_path == NULL) {
        LOG_FATAL("cli.c", "Missing required argument --search-index");
    }

    LOG_DEBUGF("cli.c", "arg index_path=%s", args->index_path);
    LOG_DEBUGF("cli.c", "arg search_index_path=%s", args->search_index_path);

    return 0;
}

int web_args_validate(web_args_t *args, int argc, const char **argv) {

    LogCtx.verbose = 1;

    if (argc < 2) {
        fprintf(stderr, "Required positional argument: PATH.\n");
        return 1;
    }

    if (args->search_index_path != NULL && args->es_url != NULL) {
        LOG_FATAL("cli.c", "--search-index and --es-url arguments are mutually exclusive.");
    }
    if (args->search_index_path != NULL && args->es_index != NULL) {
        LOG_FATAL("cli.c", "--search-index and --es-index arguments are mutually exclusive.");
    }
    if (args->search_index_path != NULL && args->es_insecure_ssl == TRUE) {
        LOG_FATAL("cli.c", "--search-index and --es-insecure_ssl arguments are mutually exclusive.");
    }

    if (args->es_url == NULL) {
        args->es_url = DEFAULT_ES_URL;
    }

    if (args->listen_address == NULL) {
        args->listen_address = DEFAULT_LISTEN_ADDRESS;
    }

    if (args->es_index == NULL) {
        args->es_index = DEFAULT_ES_INDEX;
    }

    if (args->tagline == NULL) {
        args->tagline = DEFAULT_TAGLINE;
    }

    if (args->lang == NULL) {
        args->lang = DEFAULT_LANG;
    }

    if (strlen(args->lang) != 2 && strlen(args->lang) != 5) {
        fprintf(stderr, "Invalid --lang value, see usage\n");
        return 1;
    }

    if (args->credentials != NULL) {
        char *ptr = strstr(args->credentials, ":");
        if (ptr == NULL) {
            fprintf(stderr, "Invalid --auth format, see usage\n");
            return 1;
        }

        strncpy(args->auth_user, args->credentials, (ptr - args->credentials));
        strcpy(args->auth_pass, ptr + 1);

        if (strlen(args->auth_user) == 0) {
            fprintf(stderr, "--auth username must be at least one character long");
            return 1;
        }

        args->auth_enabled = TRUE;
    } else {
        args->auth_enabled = FALSE;
    }

    if (args->tag_credentials != NULL && args->credentials != NULL) {
        fprintf(stderr, "--auth and --tag-auth are mutually exclusive");
        return 1;
    }

    if (args->tag_credentials != NULL) {
        char *ptr = strstr(args->tag_credentials, ":");
        if (ptr == NULL) {
            fprintf(stderr, "Invalid --tag-auth format, see usage\n");
            return 1;
        }

        strncpy(args->auth_user, args->tag_credentials, (ptr - args->tag_credentials));
        strcpy(args->auth_pass, ptr + 1);

        if (strlen(args->auth_user) == 0) {
            fprintf(stderr, "--tag-auth username must be at least one character long");
            return 1;
        }

        args->tag_auth_enabled = TRUE;
    } else {
        args->tag_auth_enabled = FALSE;
    }

    if (args->auth0_public_key_path != NULL || args->auth0_audience != NULL || args->auth0_client_id ||
        args->auth0_domain) {

        if (args->auth0_public_key_path == NULL) {
            fprintf(stderr, "Missing --auth0-public-key-file argument");
            return 1;
        }
        if (args->auth0_audience == NULL) {
            fprintf(stderr, "Missing --auth0-audience argument");
            return 1;
        }
        if (args->auth0_client_id == NULL) {
            fprintf(stderr, "Missing --auth0-client-id argument");
            return 1;
        }
        if (args->auth0_domain == NULL) {
            fprintf(stderr, "Missing --auth0-domain argument");
            return 1;
        }
    }

    if (args->auth0_public_key_path != NULL) {
        if (load_external_file(args->auth0_public_key_path, &args->auth0_public_key) != 0) {
            return 1;
        }

        args->auth0_enabled = TRUE;
    } else {
        args->auth0_enabled = FALSE;
    }

    args->index_count = argc - 1;
    args->indices = argv + 1;

    for (int i = 0; i < args->index_count; i++) {
        char *abs_path = abspath(args->indices[i]);
        if (abs_path == NULL) {
            LOG_FATALF("cli.c", "Index not found: %s", args->indices[i]);
        }
        free(abs_path);
    }

    if (args->search_index_path != NULL) {
        char *abs_path = abspath(args->search_index_path);
        if (abs_path == NULL) {
            LOG_FATALF("cli.c", "Search index not found: %s", args->search_index_path);
        }

        args->es_index = NULL;
        args->es_url = NULL;
        args->es_insecure_ssl = FALSE;
        args->search_backend = SQLITE_SEARCH_BACKEND;
    } else {
        args->search_backend = ES_SEARCH_BACKEND;
    }

    LOG_DEBUGF("cli.c", "arg es_url=%s", args->es_url);
    LOG_DEBUGF("cli.c", "arg es_index=%s", args->es_index);
    LOG_DEBUGF("cli.c", "arg es_insecure_ssl=%d", args->es_insecure_ssl);
    LOG_DEBUGF("cli.c", "arg search_index_path=%s", args->search_index_path);
    LOG_DEBUGF("cli.c", "arg search_backend=%d", args->search_backend);
    LOG_DEBUGF("cli.c", "arg tagline=%s", args->tagline);
    LOG_DEBUGF("cli.c", "arg dev=%d", args->dev);
    LOG_DEBUGF("cli.c", "arg listen=%s", args->listen_address);
    LOG_DEBUGF("cli.c", "arg credentials=%s", args->credentials);
    LOG_DEBUGF("cli.c", "arg tag_credentials=%s", args->tag_credentials);
    LOG_DEBUGF("cli.c", "arg auth_user=%s", args->auth_user);
    LOG_DEBUGF("cli.c", "arg auth_pass=%s", args->auth_pass);
    LOG_DEBUGF("cli.c", "arg index_count=%d", args->index_count);
    for (int i = 0; i < args->index_count; i++) {
        LOG_DEBUGF("cli.c", "arg indices[%d]=%s", i, args->indices[i]);
    }

    return 0;
}

index_args_t *index_args_create() {
    index_args_t *args = calloc(sizeof(index_args_t), 1);
    return args;
}

sqlite_index_args_t *sqlite_index_args_create() {
    sqlite_index_args_t *args = calloc(sizeof(sqlite_index_args_t), 1);
    return args;
}

web_args_t *web_args_create() {
    web_args_t *args = calloc(sizeof(web_args_t), 1);
    return args;
}
