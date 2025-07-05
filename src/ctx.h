#ifndef SIST2_CTX_H
#define SIST2_CTX_H

#include "sist.h"
#include "tpool.h"
#include "libscan/scan.h"
#include "libscan/arc/arc.h"
#include "libscan/comic/comic.h"
#include "libscan/ebook/ebook.h"
#include "libscan/font/font.h"
#include "libscan/media/media.h"
#include "libscan/ooxml/ooxml.h"
#include "libscan/text/text.h"
#include "libscan/mobi/scan_mobi.h"
#include "libscan/raw/raw.h"
#include "libscan/msdoc/msdoc.h"
#include "libscan/wpd/wpd.h"
#include "libscan/json/json.h"
#include "src/database/database.h"
#include "src/index/elastic.h"
#include "sqlite3.h"
#include "ignorelist.h"

#include <pcre.h>

typedef struct {
    struct index_t index;

    tpool_t *pool;

    int threads;
    int depth;
    int calculate_checksums;

    pcre *exclude;
    pcre_extra *exclude_extra;
    int fast;
    ignorelist_t *ignorelist;

    scan_arc_ctx_t arc_ctx;
    scan_comic_ctx_t comic_ctx;
    scan_ebook_ctx_t ebook_ctx;
    scan_font_ctx_t font_ctx;
    scan_media_ctx_t media_ctx;
    scan_ooxml_ctx_t ooxml_ctx;
    scan_text_ctx_t text_ctx;
    scan_mobi_ctx_t mobi_ctx;
    scan_raw_ctx_t raw_ctx;
    scan_msdoc_ctx_t msdoc_ctx;
    scan_wpd_ctx_t wpd_ctx;
    scan_json_ctx_t json_ctx;
} ScanCtx_t;

typedef struct {
    int verbose;
    int very_verbose;
    int no_color;
    int json_logs;
} LogCtx_t;

typedef struct {
    char *es_url;
    int es_insecure_ssl;
    es_version_t *es_version;
    char *es_index;
    int batch_size;
    tpool_t *pool;
    /**
     * Set to false when using --print
     */
    int needs_es_connection;
} IndexCtx_t;

typedef struct {
    char *es_url;
    es_version_t *es_version;
    char *es_index;
    database_t *search_db;
    int es_insecure_ssl;
    int index_count;
    char *auth_user;
    char *auth_pass;
    int auth_enabled;
    int tag_auth_enabled;

    int auth0_enabled;
    char *auth0_public_key;
    char *auth0_audience;
    char *auth0_domain;
    char *auth0_client_id;

    char *tagline;
    struct index_t indices[256];
    char lang[10];
    int dev;
    int search_backend;
} WebCtx_t;


typedef struct {
    int thread_id;
    database_t *ipc_db;
    database_t *index_db;
} ProcData_t;

extern ScanCtx_t ScanCtx;
extern WebCtx_t WebCtx;
extern IndexCtx_t IndexCtx;
extern LogCtx_t LogCtx;
extern __thread ProcData_t ProcData;


#endif
