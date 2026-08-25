#include "database.h"
#include "malloc.h"
#include "src/ctx.h"
#include <string.h>
#include <pthread.h>
#include "src/util.h"
#include "src/parsing/mime.h"

#include <time.h>
#ifndef _WIN32
#include <sys/statvfs.h>
#endif


/** Where SQLite puts the temporary files a large statement spills into */
static const char *sqlite_temp_directory() {
    const char *directory = sqlite3_temp_directory;

    if (directory == NULL) {
        directory = getenv("SQLITE_TMPDIR");
    }
    if (directory == NULL) {
        directory = getenv("TMPDIR");
    }

    if (directory != NULL) {
        return directory;
    }

    return sist_temp_dir();
}

/** Logs how much room is left where path lives, at a level that is printed without --verbose */
static void report_free_space(const char *what, const char *path) {
    if (path == NULL || *path == '\0') {
        return;
    }

    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s", path);

    // A database is a file, so its filesystem is the one its folder is on; a temporary file
    // folder is already the folder
    struct stat info;
    if (sist_stat(directory, &info) != 0 || !S_ISDIR(info.st_mode)) {
        char *slash = strrchr(directory, '/');
        if (slash == NULL) {
            strcpy(directory, ".");
        } else if (slash == directory) {
            *(slash + 1) = '\0';
        } else {
            *slash = '\0';
        }
    }

#ifdef _WIN32
    const double free_mib = sist_free_space_mib(directory);
    if (free_mib < 0) {
        return;
    }
#else
    struct statvfs stat;
    if (statvfs(directory, &stat) != 0) {
        return;
    }

    const double free_mib = (double) stat.f_bavail * (double) stat.f_frsize / (1024 * 1024);
#endif

    LOG_FATALF_NO_EXIT("database.c", "The %s (%s) is on a filesystem with %.1f MiB free", what, path, free_mib);
}

void database_fatal_sqlite_error(database_t *db, const char *file, int line, int code) {

    // "database or disk is full" names neither the database nor the disk, and the file that ran
    // out of room is rarely the one being watched
    if (code == SQLITE_FULL) {
        report_free_space("index database", sqlite3_db_filename(db->db, "main"));
        report_free_space("search index", sqlite3_db_filename(db->db, "fts"));
        report_free_space("temporary file folder", sqlite_temp_directory());
    }

    // The connection's message is the specific one, but it describes whatever happened to the
    // connection last: a code returned by a function that never touched it says "not an error"
    const char *meaning = sqlite3_errstr(code);
    const char *message = sqlite3_errmsg(db->db);

    if (message == NULL || strcmp(message, meaning) == 0) {
        LOG_FATALF("database.c", "Sqlite error @ %s:%d : (%d) %s", file, line, code, meaning);
    } else {
        LOG_FATALF("database.c", "Sqlite error @ %s:%d : (%d) %s: %s", file, line, code, meaning, message);
    }
}

static void batch_lock(database_t *db);

static void batch_unlock(database_t *db);

static void batch_write_begin(database_t *db);

static void batch_write_end(database_t *db);

static void flush_writes(database_t *db);

database_t *database_create(const char *filename, database_type_t type) {
    database_t *db = calloc(1, sizeof(database_t));

    strcpy(db->filename, filename);
    db->type = type;
    pthread_mutex_init(&db->write_mutex, NULL);

    return db;
}

int tag_matches(const char *query, const char *tag) {
    size_t query_len = strlen(query);
    size_t tag_len = strlen(tag);

    if (query_len >= tag_len) {
        return FALSE;
    }

    return strncmp(tag, query, query_len) == 0 && *(tag + query_len) == '.';
}

void tag_matches_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {

    if (argc != 1 || sqlite3_value_type(argv[0]) != SQLITE_TEXT) {
        sqlite3_result_error(ctx, "Invalid parameters", -1);
    }

    const char *tag = (const char *) sqlite3_value_text(argv[0]);

    char **tags = *(char ***) sqlite3_user_data(ctx);

    array_foreach(tags) {
        if (tag_matches(tags[i], tag)) {
            sqlite3_result_int(ctx, TRUE);
            return;
        }
    }

    sqlite3_result_int(ctx, FALSE);
}

__always_inline
static int sep_rfind(const char *str) {
    for (int i = (int) strlen(str); i >= 0; i--) {
        if (str[i] == '/') {
            return i;
        }
    }
    return -1;
}

void path_parent_func(sqlite3_context *ctx, UNUSED(int argc), sqlite3_value **argv) {
#ifdef SIST_DEBUG
    if (argc != 1 || sqlite3_value_type(argv[0]) != SQLITE_TEXT) {
        sqlite3_result_error(ctx, "Invalid parameters", -1);
    }
#endif

    const char *value = (const char *) sqlite3_value_text(argv[0]);

    int stop = sep_rfind(value);
    if (stop == -1) {
        sqlite3_result_null(ctx);
        return;
    }
    char parent[PATH_MAX * 3];
    strncpy(parent, value, stop);

    sqlite3_result_text(ctx, parent, stop, SQLITE_TRANSIENT);
}

void random_func(sqlite3_context *ctx, UNUSED(int argc), UNUSED(sqlite3_value **argv)) {
#ifdef SIST_DEBUG
    if (argc != 1 || sqlite3_value_type(argv[0]) != SQLITE_INTEGER) {
        sqlite3_result_error(ctx, "Invalid parameters", -1);
    }
#endif

    char state_buf[8] = {0,};
    long seed = sqlite3_value_int64(argv[0]);

    initstate((int) seed, state_buf, sizeof(state_buf));

    sqlite3_result_int(ctx, (int) random());
}


/**
 * spellfix1 is vendored (third-party/sqlite-spellfix), not part of the amalgamation the sqlite3
 * port builds, so every connection that so much as prepares a statement against a search index
 * has to be handed the module: a virtual table whose module is missing cannot even be opened.
 */
static void register_spellfix(database_t *db) {
    CRASH_IF_NOT_SQLITE_OK(sqlite3_spellfix_init(db->db, NULL, NULL));
}

void database_initialize(database_t *db) {
    CRASH_IF_NOT_SQLITE_OK(sqlite3_open(db->filename, &db->db));
    register_spellfix(db);

    LOG_DEBUGF("database.c", "Initializing database %s", db->filename);
    if (db->type == INDEX_DATABASE) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, IndexDatabaseSchema, NULL, NULL, NULL));
    } else if (db->type == FTS_DATABASE) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, FtsDatabaseSchema, NULL, NULL, NULL));
    }

    sqlite3_close(db->db);
}

void database_open(database_t *db) {
    LOG_DEBUGF("database.c", "Opening database %s (%d)", db->filename, db->type);

    CRASH_IF_NOT_SQLITE_OK(sqlite3_open(db->filename, &db->db));
    register_spellfix(db);
    sqlite3_busy_timeout(db->db, 1000);

    // TODO: Optional argument?
//    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA cache_size = -200000;", NULL, NULL, NULL));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA synchronous = OFF;", NULL, NULL, NULL));

    if (db->type == INDEX_DATABASE) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA temp_store = memory;", NULL, NULL, NULL));
    }

#ifdef SIST_DEBUG
        //    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL));
#else
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA ignore_check_constraints = ON;", NULL, NULL, NULL));
#endif

    if (db->type == INDEX_DATABASE) {
        // Prepare statements;
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "SELECT data FROM thumbnail WHERE id=? AND num=? LIMIT 1;", -1,
                &db->select_thumbnail_stmt, NULL));
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "UPDATE marked SET marked=1 WHERE id=(SELECT ROWID FROM document WHERE path=?) AND mtime=? RETURNING id",
                -1,
                &db->mark_document_stmt, NULL));
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "UPDATE marked SET marked=1 WHERE id=(SELECT ROWID FROM document WHERE path=?) AND mtime=? RETURNING id",
                -1,
                &db->mark_walked_document_stmt, NULL));
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "INSERT INTO document (path, parent, mime, mtime, size, thumbnail_count, json_data, version) "
                "VALUES (?, (SELECT id FROM document WHERE path=?), ?, ?, ?, ?, ?, (SELECT max(id) FROM version)) "
                "ON CONFLICT (path) DO UPDATE SET json_data=excluded.json_data, mime=excluded.mime, "
                "mtime=excluded.mtime, size=excluded.size, thumbnail_count=excluded.thumbnail_count, "
                "version=excluded.version "
                "RETURNING id;",
                -1,
                &db->write_document_stmt, NULL));
        // mark_document_stmt misses a file whose mtime changed, so mark it by id instead
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "UPDATE marked SET marked=1 WHERE id=?;",
                -1,
                &db->mark_written_document_stmt, NULL));
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "INSERT INTO thumbnail (id, num, data) VALUES (?,?,?) ON CONFLICT DO UPDATE SET data=excluded.data;",
                -1,
                &db->write_thumbnail_stmt, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT json_set(json_data, "
                        "'$._id', CAST (doc.id AS TEXT),"
                        "'$.thumbnail', doc.thumbnail_count,"
                        "'$.mime', m.name,"
                        "'$.size', doc.size"
                        ") FROM document doc LEFT JOIN mime m ON m.id=doc.mime WHERE doc.id=?", -1,
                &db->get_document, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT parent FROM document WHERE id=?", -1,
                &db->get_parent_id, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT * FROM model", -1,
                &db->get_models, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT embedding FROM embedding WHERE id=? AND model_id=? AND start=0", -1,
                &db->get_embedding, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "INSERT INTO tag (id, tag) VALUES (?,?) ON CONFLICT DO NOTHING;",
                -1,
                &db->write_tag_stmt, NULL));

        // Create functions
        sqlite3_create_function(
                db->db,
                "path_parent",
                1,
                SQLITE_UTF8,
                NULL,
                path_parent_func,
                NULL,
                NULL
        );

        sqlite3_create_function(
                db->db,
                "emb_to_json",
                1,
                SQLITE_UTF8,
                NULL,
                emb_to_json_func,
                NULL,
                NULL
        );
    } else if (db->type == FTS_DATABASE) {

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT path, count FROM path_index"
                        " WHERE (index_id=?1 OR ?1 IS NULL) AND depth BETWEEN ? AND ?"
                        " LIMIT 65536", -1,
                &db->fts_search_paths, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT json_data FROM document_index"
                        " WHERE id=?", -1,
                &db->fts_get_document, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT DISTINCT tag FROM tag"
                        " WHERE tag GLOB (? || '*') ORDER BY tag LIMIT 100", -1,
                &db->fts_suggest_tag, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT tag, count(*) FROM tag GROUP BY tag", -1,
                &db->fts_get_tags, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT size FROM model WHERE id=?", -1,
                &db->fts_model_size, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT path, count FROM path_index"
                        " WHERE (index_id=?1 OR ?1 IS NULL) AND depth BETWEEN ? AND ?"
                        " AND (path = ?4 or path GLOB ?5)"
                        " LIMIT 65536", -1,
                &db->fts_search_paths_w_prefix, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT path, count FROM path_index"
                        " WHERE depth BETWEEN ? AND ?"
                        " AND path GLOB ?"
                        " LIMIT 65536", -1,
                &db->fts_suggest_paths, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT * FROM stats", -1,
                &db->fts_date_range, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT mime, sum(count) FROM mime_index WHERE mime is not NULL GROUP BY mime", -1,
                &db->fts_get_mimetypes, NULL));

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "INSERT INTO tag (id, index_id, tag) VALUES (?,?,?) ON CONFLICT DO NOTHING;",
                -1,
                &db->fts_write_tag_stmt, NULL));

        sqlite3_create_function(
                db->db,
                "random_seeded",
                1,
                SQLITE_UTF8,
                NULL,
                random_func,
                NULL,
                NULL
        );

        sqlite3_create_function(
                db->db,
                "path_parent",
                1,
                SQLITE_UTF8,
                NULL,
                path_parent_func,
                NULL,
                NULL
        );

        sqlite3_create_function(
                db->db,
                "tag_matches",
                1,
                SQLITE_UTF8,
                &db->tag_array,
                tag_matches_func,
                NULL,
                NULL
        );

        sqlite3_create_function(
                db->db,
                "cosine_sim",
                3,
                SQLITE_UTF8,
                NULL,
                cosine_sim_func,
                NULL,
                NULL
        );
    }

    if (db->type == FTS_DATABASE || db->type == INDEX_DATABASE) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "DELETE FROM tag WHERE id=? AND tag=?;",
                -1,
                &db->delete_tag_stmt, NULL));
    }
}

// sqlite3_close() refuses to free a connection that still owns prepared statements. Only the
// ones prepared here may be finalized: fts5 keeps its own on the same connection and frees
// them itself when the virtual table is disconnected.
static void database_finalize_statements(database_t *db) {
    sqlite3_stmt **statements[] = {
            &db->select_thumbnail_stmt,
            &db->treemap_merge_up_update_stmt,
            &db->treemap_merge_up_delete_stmt,
            &db->mark_document_stmt,
            &db->mark_walked_document_stmt,
            &db->mark_written_document_stmt,
            &db->write_document_stmt,
            &db->write_thumbnail_stmt,
            &db->get_document,
            &db->get_content,
            &db->get_parent_id,
            &db->get_models,
            &db->get_embedding,
            &db->delete_tag_stmt,
            &db->write_tag_stmt,
            &db->fts_search_paths,
            &db->fts_search_paths_w_prefix,
            &db->fts_suggest_paths,
            &db->fts_date_range,
            &db->fts_get_mimetypes,
            &db->fts_get_document,
            &db->fts_suggest_tag,
            &db->fts_get_tags,
            &db->fts_write_tag_stmt,
            &db->fts_model_size,
    };

    for (size_t i = 0; i < sizeof(statements) / sizeof(statements[0]); i++) {
        sqlite3_finalize(*statements[i]);
        *statements[i] = NULL;
    }
}

void database_close(database_t *db, int optimize) {
    LOG_DEBUGF("database.c", "Closing database %s (%p)", db->filename, (void *) db->db);

    if (db->db) {
        database_flush_writes(db);
    }

    if (optimize) {
        LOG_DEBUG("database.c", "Optimizing database");
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "VACUUM;", NULL, NULL, NULL));
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA optimize;", NULL, NULL, NULL));
    }

    if (db->db) {
        database_finalize_statements(db);

        int res = sqlite3_close(db->db);
        if (res != SQLITE_OK) {
            LOG_ERRORF("database.c", "Could not close %s: (%d) %s",
                       db->filename, res, sqlite3_errmsg(db->db));
        }
    }

    free(db);
    db = NULL;
}

void *database_read_thumbnail(database_t *db, int doc_id, int num, size_t *return_value_len) {
    sqlite3_bind_int(db->select_thumbnail_stmt, 1, doc_id);
    sqlite3_bind_int(db->select_thumbnail_stmt, 2, num);

    int ret = sqlite3_step(db->select_thumbnail_stmt);

    if (ret == SQLITE_DONE) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->select_thumbnail_stmt));
        *return_value_len = 0;
        return NULL;
    }

    CRASH_IF_STMT_FAIL(ret);

    const void *blob = sqlite3_column_blob(db->select_thumbnail_stmt, 0);
    const int blob_size = sqlite3_column_bytes(db->select_thumbnail_stmt, 0);

    *return_value_len = blob_size;
    void *return_data = malloc(blob_size);
    memcpy(return_data, blob, blob_size);

    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->select_thumbnail_stmt));

    return return_data;
}

void database_write_index_descriptor(database_t *db, index_descriptor_t *desc) {
    database_flush_writes(db);


    sqlite3_exec(db->db, "DELETE FROM descriptor;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;

    sqlite3_prepare_v2(db->db, "INSERT INTO descriptor (id, version_major, version_minor, version_patch,"
                               " root, name, rewrite_url, timestamp) VALUES (?,?,?,?,?,?,?,?);", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, desc->id);
    sqlite3_bind_int(stmt, 2, desc->version_major);
    sqlite3_bind_int(stmt, 3, desc->version_minor);
    sqlite3_bind_int(stmt, 4, desc->version_patch);
    sqlite3_bind_text(stmt, 5, desc->root, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, desc->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, desc->rewrite_url, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 8, desc->timestamp);

    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));

    sqlite3_finalize(stmt);
}

index_descriptor_t *database_read_index_descriptor(database_t *db) {

    sqlite3_stmt *stmt;

    sqlite3_prepare_v2(db->db, "SELECT id, version_major, version_minor, version_patch,"
                               " root, name, rewrite_url, timestamp FROM descriptor;", -1, &stmt, NULL);

    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));

    int id = sqlite3_column_int(stmt, 0);
    int v_major = sqlite3_column_int(stmt, 1);
    int v_minor = sqlite3_column_int(stmt, 2);
    int v_patch = sqlite3_column_int(stmt, 3);
    const char *root = (char *) sqlite3_column_text(stmt, 4);
    const char *name = (char *) sqlite3_column_text(stmt, 5);
    const char *rewrite_url = (char *) sqlite3_column_text(stmt, 6);
    int64_t timestamp = sqlite3_column_int64(stmt, 7);

    index_descriptor_t *desc = malloc(sizeof(index_descriptor_t));
    desc->id = id;
    snprintf(desc->version, sizeof(desc->version), "%d.%d.%d", v_major, v_minor, v_patch);
    desc->version_major = v_major;
    desc->version_minor = v_minor;
    desc->version_patch = v_patch;
    strcpy(desc->root, root);
    strcpy(desc->name, name);
    strcpy(desc->rewrite_url, rewrite_url);
    desc->timestamp = timestamp;

    CRASH_IF_NOT_SQLITE_OK(sqlite3_finalize(stmt));

    return desc;
}

database_iterator_t *database_create_delete_list_iterator(database_t *db) {

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db->db, "SELECT id FROM delete_list", -1, &stmt, NULL);

    database_iterator_t *iter = malloc(sizeof(database_iterator_t));

    iter->stmt = stmt;
    iter->db = db;

    return iter;
}

int database_delete_list_iter(database_iterator_t *iter) {
    int ret = sqlite3_step(iter->stmt);

    if (ret == SQLITE_ROW) {
        return sqlite3_column_int(iter->stmt, 0);
    }

    if (ret != SQLITE_DONE) {
        LOG_FATALF("database.c", "FIXME: delete iter returned %s", sqlite3_errmsg(iter->db->db));
    }

    if (sqlite3_finalize(iter->stmt) != SQLITE_OK) {
        LOG_FATALF("database.c", "FIXME: delete iter returned %s", sqlite3_errmsg(iter->db->db));
    }

    iter->stmt = NULL;

    return 0;
}

database_iterator_t *database_create_document_iterator(database_t *db, long long min_version) {

    sqlite3_stmt *stmt;

    // Grouping by document id makes a table scan look cheaper than the version index to the query
    // planner, which is true for a full pass and very wrong for the handful of rows a rescan wrote
    const char *source = min_version > 0
                         ? " FROM document INDEXED BY document_version_idx"
                         : " FROM document";

    if (min_version > 0) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
                db->db, "CREATE INDEX IF NOT EXISTS document_version_idx ON document(version);",
                NULL, NULL, NULL));
    }

    char *sql = sqlite3_mprintf(
            "WITH doc (id, j) AS ("
            "SELECT"
            " document.id,"
            " json_set(document.json_data,"
            "  '$._id', document.id,"
            "  '$.index', (SELECT id FROM descriptor),"
            "  '$.size', document.size,"
            "  '$.mtime', document.mtime,"
            "  '$.mime', mim.name,"
            "  '$.thumbnail', document.thumbnail_count,"
            "  '$.tag', json_group_array(t.tag))"
            "%s"
            "  LEFT JOIN mime mim ON mim.id = document.mime"
            "  LEFT JOIN tag t ON t.id = document.id"
            " WHERE document.version > ?"
            " GROUP BY document.id),"
            // emb.<path> is a single dense_vector, so only the first chunk of a model goes in it
            " emb_doc (id, j) AS ("
            "SELECT doc.id, CASE"
            " WHEN emb.embedding IS NULL THEN j"
            " ELSE json_set(j,"
            "  '$.emb', json_group_object(m.path, json(emb_to_json(emb.embedding))),"
            "  '$.embedding', 1"
            "     ) END"
            " FROM doc"
            " LEFT JOIN embedding emb ON doc.id = emb.id AND emb.start = 0"
            " LEFT JOIN model m ON emb.model_id = m.id"
            " GROUP BY doc.id)"
            // Every chunk of every model is a nested document of its own, so that a kNN search
            // scores the passage that matched and can quote it back
            "SELECT CASE"
            " WHEN json_array_length(chunks) = 0 THEN j"
            " ELSE json_set(j, '$.emb_chunks', json(chunks), '$.embedding', 1) END"
            " FROM (SELECT emb_doc.j AS j, ("
            "  SELECT json_group_array(json_object("
            "    'start', c.start, 'end', c.\"end\", 'emb', json(c.embs)))"
            "  FROM (SELECT e.start AS start, e.\"end\" AS \"end\","
            "         json_group_object(mo.path, json(emb_to_json(e.embedding))) AS embs"
            "        FROM embedding e"
            "        INNER JOIN model mo ON mo.id = e.model_id"
            "        WHERE e.id = emb_doc.id GROUP BY e.start) c"
            " ) AS chunks FROM emb_doc)", source);

    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL));
    sqlite3_free(sql);

    sqlite3_bind_int64(stmt, 1, min_version);

    database_iterator_t *iter = malloc(sizeof(database_iterator_t));

    iter->stmt = stmt;
    iter->db = db;

    return iter;
}

/**
 * The slice of .content an embedding was generated from. Elasticsearch has no side channel to read
 * the text back from at search time the way the SQLite backend does, so the passage that matched is
 * carried in the nested document that holds its vector.
 */
static void add_chunk_text(cJSON *doc) {
    cJSON *chunks = cJSON_GetObjectItem(doc, "emb_chunks");
    const cJSON *content = cJSON_GetObjectItem(doc, "content");

    if (!cJSON_IsArray(chunks) || !cJSON_IsString(content)) {
        return;
    }

    const char *text = content->valuestring;
    const size_t text_len = strlen(text);

    cJSON *chunk;
    cJSON_ArrayForEach(chunk, chunks) {
        const cJSON *start_json = cJSON_GetObjectItem(chunk, "start");
        const cJSON *end_json = cJSON_GetObjectItem(chunk, "end");

        const double start_val = cJSON_IsNumber(start_json) ? start_json->valuedouble : -1;
        const double end_val = cJSON_IsNumber(end_json) ? end_json->valuedouble : -1;

        // A chunk that does not fall inside the text the document has now — it was written against
        // an older scan, or by a script that counted something other than bytes — falls back to all
        // of it, as the SQLite backend does
        size_t start = (start_val >= 0 && start_val < (double) text_len) ? (size_t) start_val : 0;
        size_t end = (end_val >= 0 && end_val <= (double) text_len) ? (size_t) end_val : text_len;

        if (end <= start) {
            start = 0;
            end = text_len;
        }

        start = utf8_boundary(text, start, text_len);
        end = utf8_boundary(text, end, text_len);

        char *slice = strndup(text + start, end - start);
        cJSON_AddStringToObject(chunk, "text", slice);
        free(slice);
    }
}

void remove_tag_if_null(cJSON *doc) {
    cJSON *tags = cJSON_GetObjectItem(doc, "tag");
    if (tags != NULL && cJSON_IsNull(cJSON_GetArrayItem(tags, 0))) {
        cJSON_DeleteItemFromObject(doc, "tag");
    }
}

cJSON *database_document_iter(database_iterator_t *iter) {

    if (iter->stmt == NULL) {
        LOG_ERROR("database.c", "FIXME: database_document_iter() called after iteration stopped");
        return NULL;
    }

    int ret = sqlite3_step(iter->stmt);

    if (ret == SQLITE_ROW) {
        const char *json_string = (const char *) sqlite3_column_text(iter->stmt, 0);

        cJSON *doc = cJSON_Parse(json_string);

        remove_tag_if_null(doc);
        add_chunk_text(doc);

        return doc;
    }

    if (ret != SQLITE_DONE) {
        LOG_FATALF("database.c", "FIXME: doc iter returned %s", sqlite3_errmsg(iter->db->db));
    }

    if (sqlite3_finalize(iter->stmt) != SQLITE_OK) {
        LOG_FATALF("database.c", "FIXME: doc iter returned %s", sqlite3_errmsg(iter->db->db));
    }

    iter->stmt = NULL;

    return NULL;
}

void database_incremental_scan_begin(database_t *db) {
    LOG_DEBUG("database.c", "Preparing database for incremental scan");
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DELETE FROM marked;", NULL, NULL, NULL));
    LOG_DEBUG("database.c", "Preparing database for incremental scan (create marked table)");
    CRASH_IF_NOT_SQLITE_OK(
            sqlite3_exec(db->db, "INSERT INTO marked SELECT id, 0, mtime FROM document;", NULL, NULL, NULL));
}

void database_incremental_scan_end(database_t *db) {
    database_flush_writes(db);

    // An archive that has not changed is never re-parsed, so the documents nested inside it are
    // never marked one by one. They are still in the index and still current, so anything below a
    // marked document is marked along with it.
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "WITH RECURSIVE descendant(id) AS ("
            "   SELECT d.id FROM document d INNER JOIN marked m ON m.id = d.parent AND m.marked = 1"
            "   UNION"
            "   SELECT d.id FROM document d INNER JOIN descendant p ON d.parent = p.id"
            ") "
            "UPDATE marked SET marked = 1 WHERE id IN (SELECT id FROM descendant);",
            NULL, NULL, NULL
    ));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM delete_list WHERE id IN (SELECT id FROM marked WHERE marked = 1);",
            NULL, NULL, NULL
    ));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM thumbnail WHERE EXISTS ("
            " SELECT document.id FROM document INNER JOIN marked m ON m.id = document.ROWID"
            " WHERE marked=0 and document.id = thumbnail.id)",
            NULL, NULL, NULL
    ));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "INSERT INTO delete_list (id) "
            "SELECT id FROM marked WHERE marked=0 ON CONFLICT DO NOTHING;",
            NULL, NULL, NULL
    ));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM document WHERE ROWID IN (SELECT id FROM marked WHERE marked=0);",
            NULL, NULL, NULL
    ));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM marked;",
            NULL, NULL, NULL
    ));
}

static int mark_document(database_t *db, sqlite3_stmt *stmt, const char *path, int mtime) {
    batch_lock(db);
    batch_write_begin(db);

    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, mtime);

    int ret = sqlite3_step(stmt);
    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(stmt));

    batch_write_end(db);
    batch_unlock(db);

    if (ret == SQLITE_ROW) {
        return TRUE;
    }

    if (ret == SQLITE_DONE) {
        return FALSE;
    }

    CRASH_IF_STMT_FAIL(ret);
    return FALSE;
}

int database_mark_document(database_t *db, const char *path, int mtime) {
    return mark_document(db, db->mark_document_stmt, path, mtime);
}

/** For the walk thread, which decides whether a file is worth sending to a worker at all */
int database_mark_walked_document(database_t *db, const char *path, int mtime) {
    return mark_document(db, db->mark_walked_document_stmt, path, mtime);
}

// In autocommit each row is its own transaction, which creates, commits and unlinks a
// rollback journal per document while the workers idle behind it.
#define WRITE_BATCH_SIZE 1000

/** Held across begin/statement/end, so that two threads cannot both open or close the batch */
static void batch_lock(database_t *db) {
    pthread_mutex_lock(&db->write_mutex);
}

static void batch_unlock(database_t *db) {
    pthread_mutex_unlock(&db->write_mutex);
}

static void batch_write_begin(database_t *db) {
    if (db->uncommitted_writes == 0) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "BEGIN;", NULL, NULL, NULL));
    }
}

static void batch_write_end(database_t *db) {
    db->uncommitted_writes += 1;
    if (db->uncommitted_writes >= WRITE_BATCH_SIZE) {
        flush_writes(db);
    }
}

// Must be called before anything that cannot run inside a transaction (VACUUM) or
// that reads the written rows from another connection.
static void flush_writes(database_t *db) {
    if (db->uncommitted_writes > 0) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "COMMIT;", NULL, NULL, NULL));
        db->uncommitted_writes = 0;
    }
}

void database_flush_writes(database_t *db) {
    batch_lock(db);
    flush_writes(db);
    batch_unlock(db);
}

int database_write_document(database_t *db, document_t *doc, const char *json_data) {

    batch_lock(db);
    batch_write_begin(db);

    const char *rel_path = doc->filepath + ScanCtx.index.desc.root_len;
    const char *parent_rel_path = doc->parent[0] != '\0'
                                  ? doc->parent + ScanCtx.index.desc.root_len
                                  : NULL;

    // path, parent, mtime, size, json_data
    sqlite3_bind_text(db->write_document_stmt, 1, rel_path, -1, SQLITE_STATIC);
    sqlite3_bind_text(db->write_document_stmt, 2, parent_rel_path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(db->write_document_stmt, 3, doc->mime);
    sqlite3_bind_int(db->write_document_stmt, 4, doc->mtime);
    sqlite3_bind_int64(db->write_document_stmt, 5, (int64_t) doc->size);
    sqlite3_bind_int(db->write_document_stmt, 6, doc->thumbnail_count);
    if (json_data) {
        sqlite3_bind_text(db->write_document_stmt, 7, json_data, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(db->write_document_stmt, 7);
    }

    CRASH_IF_STMT_FAIL(sqlite3_step(db->write_document_stmt));
    int id = sqlite3_column_int(db->write_document_stmt, 0);
    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->write_document_stmt));

    // No-op outside an incremental scan
    sqlite3_bind_int(db->mark_written_document_stmt, 1, id);
    CRASH_IF_STMT_FAIL(sqlite3_step(db->mark_written_document_stmt));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->mark_written_document_stmt));

    batch_write_end(db);
    batch_unlock(db);

    return id;
}


void database_write_thumbnail(database_t *db, int doc_id, int num, void *data, size_t data_size) {
    batch_lock(db);
    batch_write_begin(db);

    sqlite3_bind_int(db->write_thumbnail_stmt, 1, doc_id);
    sqlite3_bind_int(db->write_thumbnail_stmt, 2, num);
    sqlite3_bind_blob(db->write_thumbnail_stmt, 3, data, (int) data_size, SQLITE_STATIC);

    CRASH_IF_STMT_FAIL(sqlite3_step(db->write_thumbnail_stmt));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->write_thumbnail_stmt));

    batch_write_end(db);
    batch_unlock(db);
}


void database_write_tag(database_t *db, int64_t sid, char *tag) {
    sqlite3_bind_int64(db->write_tag_stmt, 1, sid);
    sqlite3_bind_int(db->write_tag_stmt, 2, (int) (sid >> 32));
    sqlite3_bind_text(db->write_tag_stmt, 3, tag, -1, SQLITE_STATIC);

    CRASH_IF_STMT_FAIL(sqlite3_step(db->write_tag_stmt));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->write_tag_stmt));
}

void database_delete_tag(database_t *db, int64_t sid, char *tag) {
    sqlite3_bind_int64(db->delete_tag_stmt, 1, sid);
    sqlite3_bind_text(db->delete_tag_stmt, 2, tag, -1, SQLITE_STATIC);

    CRASH_IF_STMT_FAIL(sqlite3_step(db->delete_tag_stmt));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->delete_tag_stmt));
}

cJSON *database_get_document(database_t *db, int doc_id) {
    sqlite3_bind_int(db->get_document, 1, doc_id);

    int ret = sqlite3_step(db->get_document);
    CRASH_IF_STMT_FAIL(ret);

    cJSON *json;

    if (ret == SQLITE_ROW) {
        const char *json_str = (char *) sqlite3_column_text(db->get_document, 0);
        json = cJSON_Parse(json_str);
    } else {
        json = NULL;
    }

    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->get_document));

    return json;
}

int database_get_parent_id(database_t *db, int doc_id) {
    sqlite3_bind_int(db->get_parent_id, 1, doc_id);

    int ret = sqlite3_step(db->get_parent_id);
    CRASH_IF_STMT_FAIL(ret);

    int parent_id = DATABASE_NO_PARENT;

    if (ret == SQLITE_ROW && sqlite3_column_type(db->get_parent_id, 0) != SQLITE_NULL) {
        parent_id = sqlite3_column_int(db->get_parent_id, 0);
    }

    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->get_parent_id));

    return parent_id;
}

void database_increment_version(database_t *db) {
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db, "INSERT INTO version DEFAULT VALUES", NULL, NULL, NULL));
}

/** Extracted text of a document, or NULL when it has none. Caller frees. */
char *database_get_content(database_t *db, int doc_id) {
    if (db->get_content == NULL) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db, "SELECT json_data ->> 'content' FROM document WHERE id = ?", -1,
                &db->get_content, NULL));
    }

    sqlite3_bind_int(db->get_content, 1, doc_id);

    char *content = NULL;
    if (sqlite3_step(db->get_content) == SQLITE_ROW) {
        const char *text = (const char *) sqlite3_column_text(db->get_content, 0);
        if (text != NULL) {
            content = strdup(text);
        }
    }

    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->get_content));

    return content;
}

long long database_get_version(database_t *db) {
    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(db->db, "SELECT max(id) FROM version", -1, &stmt, NULL));
    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));

    long long version = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);

    return version;
}

void database_sync_mime_table(database_t *db) {
    unsigned int *cur = get_mime_ids();

    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare(
            db->db,
            "REPLACE INTO mime (id, name) VALUES (?,?)", -1, &stmt, NULL));

    while (*cur != 0) {
        sqlite3_bind_int64(stmt, 1, (int64_t) *cur);
        sqlite3_bind_text(stmt, 2, mime_get_mime_text(*cur), -1, NULL);

        CRASH_IF_STMT_FAIL(sqlite3_step(stmt));
        sqlite3_reset(stmt);

        cur += 1;
    }
    sqlite3_finalize(stmt);
}