#include "database.h"
#include "src/ctx.h"
#include "src/web/highlight.h"

// A name is a few words, and all of them belong in the highlight
#define NAME_CONTEXT_WORDS 64

#define ASPRINTF_OR_FATAL(...) do { \
    if (asprintf(__VA_ARGS__) == -1) { \
        LOG_FATAL("database_fts.c", "asprintf() failed"); \
    }} while (0)

void database_fts_detach(database_t *db) {
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db, "DETACH DATABASE fts",
            NULL, NULL, NULL
    ));
}

void database_fts_attach(database_t *db, const char *fts_database_path) {

    LOG_DEBUGF("database_fts.c", "Attaching to %s", fts_database_path);

    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
            db->db, "ATTACH DATABASE ? AS fts"
                    "", -1, &stmt, NULL));

    sqlite3_bind_text(stmt, 1, fts_database_path, -1, SQLITE_STATIC);

    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));
    sqlite3_finalize(stmt);

    // Unqualified PRAGMAs only reach the main database, so the attached search
    // index keeps the default synchronous=FULL and fsyncs its way through the
    // whole FTS build.
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA fts.synchronous = OFF;",
                                        NULL, NULL, NULL));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA fts.journal_mode = MEMORY;",
                                        NULL, NULL, NULL));

    // A search index built before the text moved out of it keeps its own copy of every document,
    // and fts5 will not change the shape of an existing table
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
            db->db, "SELECT sql FROM fts.sqlite_master WHERE name = 'search'", -1, &stmt, NULL));

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *sql = (const char *) sqlite3_column_text(stmt, 0);

        if (sql != NULL && strstr(sql, "content=''") == NULL) {
            sqlite3_finalize(stmt);
            LOG_FATALF("database_fts.c",
                       "Search index %s was built by an older version of sist2. Delete it and run "
                       "sqlite-index again to rebuild it.", fts_database_path);
        }
    }

    sqlite3_finalize(stmt);
}

int database_fts_get_max_path_depth(database_t *db) {
    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
            db->db, "SELECT MAX(depth) FROM path_tmp", -1, &stmt, NULL));
    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));

    int max_depth = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    return max_depth;
}

long long database_fts_scalar(database_t *db, const char *sql, long long fallback) {
    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL));

    long long value = fallback;
    int ret = sqlite3_step(stmt);
    CRASH_IF_STMT_FAIL(ret);

    if (ret == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        value = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);

    return value;
}

static void fts_exec_with_version(database_t *db, const char *sql, long long version) {
    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL));
    sqlite3_bind_int64(stmt, 1, version);
    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));
    sqlite3_finalize(stmt);
}

static void fts_set_state(database_t *db, long long version, int dirty, long long documents) {
    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
            db->db,
            "INSERT INTO fts.index_state (index_id, version, dirty, documents)"
            " VALUES ((SELECT id FROM descriptor), ?, ?, ?)"
            " ON CONFLICT (index_id) DO UPDATE SET version=excluded.version, dirty=excluded.dirty,"
            "  documents=excluded.documents",
            -1, &stmt, NULL));
    sqlite3_bind_int64(stmt, 1, version);
    sqlite3_bind_int(stmt, 2, dirty);
    sqlite3_bind_int64(stmt, 3, documents);
    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));
    sqlite3_finalize(stmt);
}

void database_fts_index(database_t *db, int rebuild, int skip_spellfix) {

    // An index database created by an older version does not have it, and finding the changed
    // documents without it means scanning the whole document table
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db, "CREATE INDEX IF NOT EXISTS document_version_idx ON document(version);",
            NULL, NULL, NULL));

    long long source_version = database_fts_scalar(db, "SELECT max(id) FROM version", 0);
    long long indexed_version = database_fts_scalar(
            db,
            "SELECT version FROM fts.index_state"
            " WHERE index_id = (SELECT id FROM descriptor) AND dirty = 0",
            0);
    long long own_documents = database_fts_scalar(
            db, "SELECT documents FROM fts.index_state WHERE index_id = (SELECT id FROM descriptor)", 0);
    long long all_documents = database_fts_scalar(db, "SELECT sum(documents) FROM fts.index_state", 0);

    // Documents keep the version of the scan that last wrote them, so anything above the version
    // this search index was built from is what needs to be re-tokenised.
    int incremental = !rebuild && indexed_version > 0 && indexed_version <= source_version;

    if (!incremental) {
        indexed_version = 0;
    }

    fts_set_state(db, indexed_version, TRUE, own_documents);

    long long changed = 0;

    if (incremental) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
                db->db,
                "CREATE TEMP TABLE fts_changed (id INTEGER PRIMARY KEY);", NULL, NULL, NULL));

        fts_exec_with_version(
                db,
                "INSERT INTO fts_changed (id)"
                " SELECT ((SELECT id FROM descriptor) << 32) | id FROM document WHERE version > ?",
                indexed_version);

        changed = database_fts_scalar(db, "SELECT count(*) FROM fts_changed", 0);

        // Deleting a row one at a time costs more than re-tokenising it, so past a certain share of
        // the index it is cheaper to throw the whole search table away and start over.
        if (changed * 2 > all_documents) {
            LOG_DEBUGF("database_fts.c", "%lld of %lld documents changed, rebuilding instead",
                       changed, all_documents);
            incremental = FALSE;
            indexed_version = 0;

            CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DROP TABLE fts_changed;", NULL, NULL, NULL));
        }
    }

    if (!incremental) {
        changed = database_fts_scalar(db, "SELECT count(*) FROM document WHERE version > 0", 0);
    }

    LOG_INFOF("database_fts.c", "Creating content table (%s, source version %lld, indexed version %lld)",
              incremental ? "incremental" : "full", source_version, indexed_version);

    long long new_documents = 0;

    if (incremental) {
        new_documents = database_fts_scalar(
                db,
                "SELECT count(*) FROM fts_changed"
                " WHERE NOT EXISTS (SELECT 1 FROM fts.document_index d WHERE d.id = fts_changed.id)", 0);

        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
                db->db,
                "INSERT OR IGNORE INTO fts_changed (id)"
                " SELECT ((SELECT id FROM descriptor) << 32) | id FROM delete_list;", NULL, NULL, NULL));

        changed = database_fts_scalar(db, "SELECT count(*) FROM fts_changed", 0);

        if (changed == 0) {
            LOG_INFO("database_fts.c", "Search index is up to date");

            CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DROP TABLE fts_changed;", NULL, NULL, NULL));
            fts_set_state(db, source_version, FALSE, own_documents);

            // Nothing was tokenised, so the vocabulary is up to date as well — unless it was
            // never built, which is what a search index made by an older version looks like
            if (skip_spellfix) {
                // The index is left without one, however up to date it is
                database_fts_drop_vocab(db);
            } else if (!database_fts_has_vocab(db)) {
                database_fts_build_vocab(db, TRUE, FALSE);
            }
            return;
        }

        LOG_DEBUG("database_fts.c", "Removing changed documents from the search index");

        // Only the rows that are actually in the index: a tombstone for a row fts5 never saw
        // would stay there forever.
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
                db->db,
                "DELETE FROM search WHERE rowid IN ("
                " SELECT id FROM fts_changed WHERE id IN (SELECT id FROM fts.document_index));",
                NULL, NULL, NULL));
    } else {
        // Merging while the whole corpus is being inserted is wasted work: the segments it merges
        // are superseded seconds later. Off for the bulk load, back to the fts5 default after it,
        // so the incremental runs that follow keep the segment count in check.
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
                db->db, "INSERT INTO search(search, rank) VALUES ('automerge', 0)",
                NULL, NULL, NULL));

        // Only this index: the other indices in a shared search database were built from source
        // databases that are not attached here, so their rows could not be inserted back.
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
                db->db,
                "DELETE FROM search WHERE rowid IN ("
                " SELECT id FROM fts.document_index WHERE index_id = (SELECT id FROM descriptor));",
                NULL, NULL, NULL));
    }

    fts_exec_with_version(
            db,
            "WITH docs AS ("
            " SELECT "
            "  ((SELECT id FROM descriptor) << 32) | document.id as id,"
            "  (SELECT id FROM descriptor) as index_id,"
            "  size,"
            "  document.json_data ->> 'name' as name,"
            "  document.json_data ->> 'path' as path,"
            "  mtime,"
            "  m.name as mime,"
            "  thumbnail_count,"
            // The text is what the search index is built from, not what it stores
            "  json_remove(document.json_data, '$.content')"
            " FROM document"
            " LEFT JOIN mime m ON m.id=document.mime"
            " WHERE document.version > ?"
            " )"
            " INSERT"
            " INTO fts.document_index (id, index_id, size, name, path, mtime, mime, thumbnail_count, json_data)"
            " SELECT * FROM docs WHERE true"
            " on conflict (id) do update set "
            "  size=excluded.size, mtime=excluded.mtime, mime=excluded.mime, json_data=excluded.json_data;",
            indexed_version);

    LOG_DEBUG("database_fts.c", "Copying embeddings");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "REPLACE INTO fts.model (id, size)"
            " SELECT id, size FROM model", NULL, NULL, NULL));

    fts_exec_with_version(
            db,
            "REPLACE INTO fts.embedding (id, model_id, start, end, embedding)"
            " SELECT (SELECT id FROM descriptor) << 32 | id, model_id, start, end, embedding FROM embedding "
            " WHERE id IN (SELECT id FROM document WHERE version > ?)"
            " ON CONFLICT (id, model_id, start) DO NOTHING;", indexed_version);

    // TODO: delete old embeddings

    LOG_DEBUG("database_fts.c", "Deleting old documents");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM fts.document_index"
            " WHERE index_id = (SELECT id FROM descriptor)"
            "  AND id IN (SELECT ((SELECT id FROM descriptor) << 32) | id FROM delete_list);",
            NULL, NULL, NULL));

    long long deleted_documents = sqlite3_changes(db->db);

    LOG_DEBUG("database_fts.c", "Generating summary stats");
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM fts.stats", NULL, NULL, NULL));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db, "INSERT INTO fts.stats "
                    "SELECT min(mtime), max(mtime) FROM fts.document_index",
            NULL, NULL, NULL));

    LOG_DEBUG("database_fts.c", "Generating mime index");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db, "DELETE FROM fts.mime_index;", NULL, NULL, NULL));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db, "INSERT INTO fts.mime_index (index_id, mime, count) "
                    "SELECT index_id, mime, count(*) FROM fts.document_index "
                    "WHERE mime IS NOT NULL "
                    "GROUP BY index_id, mime",
            NULL, NULL, NULL));

    LOG_DEBUG("database_fts.c", "Generating path index");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "CREATE TEMP TABLE path_tmp ("
            " path TEXT,"
            " index_id TEXT,"
            " count INTEGER NOT NULL,"
            " depth INTEGER NOT NULL,"
            " children INTEGER NOT NULL DEFAULT(0),"
            " total INTEGER AS (count + children),"
            " PRIMARY KEY (path, index_id)"
            ");", NULL, NULL, NULL));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "INSERT INTO path_tmp (path, index_id, count, depth)"
            " SELECT path, index_id, count(*), CASE WHEN length(path) == 0 THEN 0"
            " ELSE 1 + length(path) - length(REPLACE(path, '/', ''))"
            " END as depth FROM document_index WHERE depth > 0"
            " GROUP BY path", NULL, NULL, NULL));

    int max_depth = database_fts_get_max_path_depth(db);

    for (int i = max_depth; i > 1; i--) {
        sqlite3_stmt *stmt;

        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "INSERT INTO path_tmp (path, index_id, children, depth, count)"
                " SELECT path_parent(path) parent, index_id, (SELECT COALESCE(sum(count), 0) FROM path_tmp WHERE path "
                " BETWEEN path_parent(p.path) || '/' AND path_parent(p.path) || '/𘚟' AND index_id = p.index_id) as cnt, depth-1, 0 "
                " FROM path_tmp p WHERE depth=? GROUP BY parent"
                " ON CONFLICT(path, index_id) DO UPDATE SET children=excluded.children",
                -1, &stmt, NULL));
        sqlite3_bind_int(stmt, 1, i);
        CRASH_IF_STMT_FAIL(sqlite3_step(stmt));

        LOG_DEBUGF("database_fts.c", "Path index depth %d (%d)", i, sqlite3_changes(db->db));

        sqlite3_finalize(stmt);
    }

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM path_index;"
            "INSERT INTO path_index (path, index_id, count, depth) SELECT path, index_id, total, depth FROM path_tmp",
            NULL, NULL, NULL));

    LOG_DEBUG("database_fts.c", "Generating search index");

    fts_exec_with_version(
            db,
            "INSERT INTO search(rowid, name, content, title, path)"
            " SELECT ((SELECT id FROM descriptor) << 32) | id,"
            "  json_data ->> 'name', json_data ->> 'content',"
            "  json_data ->> 'title', json_data ->> 'path'"
            " FROM document WHERE version > ?", indexed_version);

    if (!incremental) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
                db->db, "INSERT INTO search(search, rank) VALUES ('automerge', 4)",
                NULL, NULL, NULL));
    }

    if (incremental) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DROP TABLE fts_changed;", NULL, NULL, NULL));
    }

    if (skip_spellfix) {
        // A vocabulary that is not kept up to date corrects spellings to words the corpus no
        // longer has, so the flag leaves the index without one rather than with a stale one
        database_fts_drop_vocab(db);
    } else {
        // A run that only added documents cannot have taken a word away from the corpus
        const int prune = incremental && changed > new_documents;

        database_fts_build_vocab(db, !incremental, prune);
    }

    // A full run indexed every document of this index, so the changed set is the document count.
    long long documents = incremental ? own_documents + new_documents - deleted_documents : changed;

    fts_set_state(db, source_version, FALSE, documents);
}

void database_fts_optimize(database_t *db) {
    LOG_INFO("database_fts.c", "Optimizing search index");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "INSERT INTO search(search) VALUES('optimize');",
            NULL, NULL, NULL));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "PRAGMA fts.optimize;", NULL, NULL, NULL));
}

cJSON *database_fts_get_paths(database_t *db, int index_id, int depth_min, int depth_max, const char *prefix,
                              int suggest) {

    sqlite3_stmt *stmt;

    if (suggest) {
        stmt = db->fts_suggest_paths;
        sqlite3_bind_int(stmt, 1, depth_min);
        sqlite3_bind_int(stmt, 2, depth_max);

        if (prefix) {
            char *prefix_glob = malloc(strlen(prefix) + 2);
            sprintf(prefix_glob, "%s*", prefix);
            sqlite3_bind_text(stmt, 3, prefix_glob, -1, SQLITE_TRANSIENT);
            free(prefix_glob);
        }

    } else if (prefix) {
        stmt = db->fts_search_paths_w_prefix;
        if (index_id) {
            sqlite3_bind_int(stmt, 1, index_id);
        } else {
            sqlite3_bind_null(stmt, 1);
        }
        sqlite3_bind_int(stmt, 2, depth_min);
        sqlite3_bind_int(stmt, 3, depth_max);

        char *prefix_glob = malloc(strlen(prefix) + 3);
        sprintf(prefix_glob, "%s/*", prefix);
        sqlite3_bind_text(stmt, 4, prefix, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, prefix_glob, -1, SQLITE_TRANSIENT);
        free(prefix_glob);
    } else {
        stmt = db->fts_search_paths;
        if (index_id) {
            sqlite3_bind_int(stmt, 1, index_id);
        } else {
            sqlite3_bind_null(stmt, 1);
        }
        sqlite3_bind_int(stmt, 2, depth_min);
        sqlite3_bind_int(stmt, 3, depth_max);
    }

    cJSON *json = cJSON_CreateArray();

    int ret;
    do {
        ret = sqlite3_step(stmt);
        CRASH_IF_STMT_FAIL(ret);

        if (ret == SQLITE_DONE) {
            break;
        }

        cJSON *row = cJSON_CreateObject();

        cJSON_AddStringToObject(row, "path", (const char *) sqlite3_column_text(stmt, 0));
        cJSON_AddNumberToObject(row, "count", (double) sqlite3_column_int64(stmt, 1));

        cJSON_AddItemToArray(json, row);
    } while (TRUE);

    sqlite3_reset(stmt);

    return json;
}

cJSON *database_fts_get_mimetypes(database_t *db) {

    cJSON *json = cJSON_CreateArray();

    int ret;
    do {
        ret = sqlite3_step(db->fts_get_mimetypes);
        CRASH_IF_STMT_FAIL(ret);

        if (ret == SQLITE_DONE) {
            break;
        }

        cJSON *row = cJSON_CreateObject();

        cJSON_AddStringToObject(row, "mime", (const char *) sqlite3_column_text(db->fts_get_mimetypes, 0));
        cJSON_AddNumberToObject(row, "count", (double) sqlite3_column_int64(db->fts_get_mimetypes, 1));

        cJSON_AddItemToArray(json, row);
    } while (TRUE);

    sqlite3_reset(db->fts_get_mimetypes);

    return json;
}

const char *size_where_clause(int64_t size_min, int64_t size_max) {
    if (size_min > 0 && size_max > 0) {
        return "size BETWEEN @size_min AND @size_max";
    } else if (size_min > 0) {
        return "size >= @size_min";
    } else if (size_max > 0) {
        return "size <= @size_max";
    }

    return NULL;
}

const char *date_where_clause(int64_t date_min, int64_t date_max) {
    if (date_min > 0 && date_max > 0) {
        return "mtime BETWEEN @date_min AND @date_max";
    } else if (date_min > 0) {
        return "mtime >= @date_min";
    } else if (date_max > 0) {
        return "mtime <= @date_max";
    }

    return NULL;
}

int array_length(char **arr) {
    if (arr == NULL) {
        return 0;
    }

    int count = -1;
    while (arr[++count] != NULL);

    return count;
}

int int_array_length(const int *arr) {
    if (arr == NULL) {
        return 0;
    }

    int count = -1;
    while (arr[++count] != 0);

    return count;
}

#define INDEX_ID_PARAM_OFFSET (10)
#define MIME_PARAM_OFFSET (INDEX_ID_PARAM_OFFSET + 1000)
// Two parameters per path: the folder itself, and everything under it
#define PATH_PARAM_OFFSET (MIME_PARAM_OFFSET + 1000)

char *build_where_clause(const char *path_where, const char *size_where, const char *date_where,
                         const char *index_id_where, const char *mime_where, const char *query_where,
                         const char *after_where, const char *tags_where) {
    char *where = calloc(
            strlen(index_id_where)
            + (query_where ? strlen(query_where) + sizeof(" AND ") : 0)
            + (path_where ? strlen(path_where) + sizeof(" AND ") : 0)
            + (size_where ? strlen(size_where) + sizeof(" AND ") : 0)
            + (date_where ? strlen(date_where) + sizeof(" AND ") : 0)
            + (after_where ? strlen(after_where) + sizeof(" AND ") : 0)
            + (tags_where ? strlen(tags_where) + sizeof(" AND ") : 0)
            + (mime_where ? strlen(mime_where) + sizeof(" AND ") : 0) + 1,
            sizeof(char)
    );

    strcat(where, index_id_where);
    if (query_where) {
        strcat(where, " AND ");
        strcat(where, query_where);
    }
    if (path_where) {
        strcat(where, " AND ");
        strcat(where, path_where);
    }
    if (size_where) {
        strcat(where, " AND ");
        strcat(where, size_where);
    }
    if (date_where) {
        strcat(where, " AND ");
        strcat(where, date_where);
    }
    if (mime_where) {
        strcat(where, " AND ");
        strcat(where, mime_where);
    }
    if (after_where) {
        strcat(where, " AND ");
        strcat(where, after_where);
    }
    if (tags_where) {
        strcat(where, " AND ");
        strcat(where, tags_where);
    }
    return where;
}

char *index_ids_where_clause(int *index_ids) {
    int param_count = int_array_length(index_ids);

    if (param_count == 0) {
        // Always the first term of the where clause, so it cannot be omitted
        return strdup("1");
    }

    char *clause = malloc(13 + 2 + 6 * param_count);

    strcpy(clause, "index_id IN (");
    for (int i = 0; i < param_count; i++) {
        char param[16];
        snprintf(param, sizeof(param), "?%d%s",
                 INDEX_ID_PARAM_OFFSET + i, i == param_count - 1 ? "" : ",");
        strcat(clause, param);
    }
    strcat(clause, ")");

    return clause;
}

char *mime_types_where_clause(char **mime_types) {
    int param_count = array_length(mime_types);

    if (param_count == 0) {
        return NULL;
    }

    char *clause = malloc(9 + 2 + 6 * param_count);

    strcpy(clause, "mime IN (");
    for (int i = 0; i < param_count; i++) {
        char param[16];
        snprintf(param, sizeof(param), "?%d%s",
                 MIME_PARAM_OFFSET + i, i == param_count - 1 ? "" : ",");
        strcat(clause, param);
    }
    strcat(clause, ")");

    return clause;
}

char *path_where_clause(char **paths) {
    int param_count = array_length(paths);

    if (param_count == 0) {
        return NULL;
    }

    // Qualified: the fts5 search table also has a path column, and an unqualified
    // reference is ambiguous in every query that joins the two.
    char *clause = malloc(2 + param_count * 64);

    strcpy(clause, "(");
    for (int i = 0; i < param_count; i++) {
        char term[64];
        snprintf(term, sizeof(term), "doc.path = ?%d or doc.path GLOB ?%d%s",
                 PATH_PARAM_OFFSET + i * 2, PATH_PARAM_OFFSET + i * 2 + 1,
                 i == param_count - 1 ? "" : " or ");
        strcat(clause, term);
    }
    strcat(clause, ")");

    return clause;
}

const char *get_sort_var(fts_sort_t sort) {

    switch (sort) {
        case FTS_SORT_SCORE:
            // Round to 14 decimal places to avoid precision problems when converting to JSON...
            return "round(rank, 14)";
        case FTS_SORT_SIZE:
            return "size";
        case FTS_SORT_MTIME:
            return "mtime";
        case FTS_SORT_RANDOM:
            return "random_seeded(doc.ROWID + ?5)";
        case FTS_SORT_NAME:
            return "doc.name";
        case FTS_SORT_ID:
            return "doc.id";
        case FTS_SORT_EMBEDDING:
            // A document has one embedding per chunk of its content, and ranks by its best one.
            // -1, not NULL, for a document the model has no embedding for: the sort cursor is
            // read back as a number.
            return "COALESCE((SELECT MAX(cosine_sim(?7, ?8, emb.embedding)) FROM embedding emb"
                   " WHERE emb.id = doc.id AND emb.model_id = ?9), -1)";
        default:
            return NULL;
    }
}

const char *match_where(const char *query) {
    if (query == NULL || strlen(query) == 0) {
        return NULL;
    } else {
        return "search MATCH ?1";
    }
}

char *tags_where_clause(char **tags) {
    if (tags == NULL) {
        return NULL;
    }

    return "EXISTS (SELECT 1 FROM tag WHERE id=doc.id AND tag_matches(tag))";
}

database_summary_stats_t database_fts_get_date_range(database_t *db) {

    int ret = sqlite3_step(db->fts_date_range);
    CRASH_IF_STMT_FAIL(ret);

    if (ret == SQLITE_DONE) {
        return (database_summary_stats_t) {0, 0};
    }

    database_summary_stats_t stats;
    stats.date_min = (double) sqlite3_column_int64(db->fts_date_range, 0);
    stats.date_max = (double) sqlite3_column_int64(db->fts_date_range, 1);

    sqlite3_reset(db->fts_date_range);

    return stats;
}

// The ranked subquery has no sort_var alias, and only ever sorts ascending.
static const char *get_ranked_after_where(char **after) {
    if (after == NULL) {
        return NULL;
    }
    return "(rank, doc.ROWID) > (?3, ?4)";
}

char *get_after_where(char **after, UNUSED(fts_sort_t sort), int sort_asc) {
    if (after == NULL) {
        return NULL;
    }

    // One tuple comparison, so the ROWID tiebreaker in the ORDER BY runs in the same direction:
    // a descending sort whose sort_var ties would otherwise skip every row of the tie but one
    if (sort_asc) {
        return "(sort_var, doc.ROWID) > (?3, ?4)";
    }

    return "(sort_var, doc.ROWID) < (?3, ?4)";
}

int database_fts_get_model_size(database_t *db, int model_id) {
    sqlite3_bind_int(db->fts_model_size, 1, model_id);
    int ret = sqlite3_step(db->fts_model_size);
    CRASH_IF_STMT_FAIL(ret);

    if (ret == SQLITE_DONE) {
        return -1;
    }

    int size = sqlite3_column_int(db->fts_model_size, 0);
    sqlite3_reset(db->fts_model_size);

    return size;
}

/** The database the documents of an index live in, or NULL when it is not loaded */
static database_t *index_database(int index_id) {
    for (int i = 0; i < WebCtx.index_count; i++) {
        if (WebCtx.indices[i].desc.id == index_id) {
            return WebCtx.indices[i].db;
        }
    }

    return NULL;
}

/**
 * The chunk of a document that best matches the query embedding, as a byte range of its .content.
 * A model with one embedding per document answers with the whole of it (start 0, end NULL).
 */
static void best_chunk(sqlite3_stmt *stmt, long long id, long long *start, long long *end) {
    *start = -1;
    *end = -1;

    sqlite3_bind_int64(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *start = sqlite3_column_int64(stmt, 0);
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            *end = sqlite3_column_int64(stmt, 1);
        }
    }

    sqlite3_reset(stmt);
}

/**
 * fts5 cannot build the snippets: a contentless table has no text to quote from. The text is read
 * back from the index database the document came from, for the documents of this page only.
 *
 * chunk_start and chunk_end are the byte range of .content the embedding that matched was
 * generated from, or -1 for the whole of it. The query terms are still marked inside it, so an
 * embeddings search that also carries a query reads the way a plain one does.
 */
/**
 * The page each fragment of the excerpt was taken from, so that a result can be opened at the page
 * it matched on. Paginated documents only.
 */
static void add_fragment_pages(cJSON *row, const cJSON *source, const char *content,
                               const char *const *fragments, int fragment_count) {
    const cJSON *page_breaks = cJSON_GetObjectItem(source, "page_breaks");

    if (!cJSON_IsString(page_breaks)) {
        return;
    }

    int break_count;
    size_t *breaks = highlight_parse_page_breaks(page_breaks->valuestring, &break_count);

    if (breaks == NULL) {
        return;
    }

    cJSON *pages = cJSON_CreateArray();

    for (int i = 0; i < fragment_count; i++) {
        const int page = highlight_fragment_page(content, fragments[i], breaks, break_count);
        cJSON_AddItemToArray(pages, cJSON_CreateNumber(page));
    }

    cJSON_AddItemToObject(row, "hit_pages", pages);
    free(breaks);
}

static void add_highlight(cJSON *row, cJSON *source, long long id, char **terms, int context_size,
                          long long chunk_start, long long chunk_end) {
    char *const no_terms[] = {NULL};
    char *const *use_terms = terms == NULL ? no_terms : terms;

    cJSON *highlight = cJSON_AddObjectToObject(row, "highlight");

    const cJSON *name = cJSON_GetObjectItem(source, "name");
    if (cJSON_IsString(name)) {
        char *marked = highlight_text(name->valuestring, use_terms, NAME_CONTEXT_WORDS);
        if (marked != NULL) {
            cJSON_AddStringToObject(highlight, "name", marked);
            free(marked);
        }
    }

    const cJSON *index_id = cJSON_GetObjectItem(source, "index");
    database_t *index_db = cJSON_IsNumber(index_id) ? index_database(index_id->valueint) : NULL;

    if (index_db == NULL) {
        return;
    }

    char *content = database_get_content(index_db, (int) (id & 0xFFFFFFFF));
    if (content == NULL) {
        return;
    }

    const size_t content_len = strlen(content);

    // A chunk that does not fall inside the text the document has now — it was written against an
    // older scan, or by a script that counted something other than bytes — falls back to all of it
    size_t start = (chunk_start >= 0 && (size_t) chunk_start < content_len) ? (size_t) chunk_start : 0;
    size_t end = (chunk_end >= 0 && (size_t) chunk_end <= content_len) ? (size_t) chunk_end : content_len;

    if (end <= start) {
        start = 0;
        end = content_len;
    }

    start = utf8_boundary(content, start, content_len);
    end = utf8_boundary(content, end, content_len);

    if (chunk_start >= 0) {
        cJSON *chunk = cJSON_AddObjectToObject(row, "chunk");
        cJSON_AddNumberToObject(chunk, "start", (double) start);
        cJSON_AddNumberToObject(chunk, "end", (double) end);
    }

    char *chunk_text = (start == 0 && end == content_len) ? content : strndup(content + start, end - start);

    char *marked = highlight_text(chunk_text, use_terms, context_size);
    if (marked != NULL) {
        cJSON_AddStringToObject(highlight, "content", marked);
        add_fragment_pages(row, source, content, (const char *const[]) {marked}, 1);
        free(marked);
    }

    if (chunk_text != content) {
        free(chunk_text);
    }
    free(content);
}

cJSON *database_fts_search(database_t *db, const char *query, char **paths, int64_t size_min,
                           int64_t size_max, int64_t date_min, int64_t date_max, int page_size,
                           int *index_ids, char **mime_types, char **tags, int sort_asc,
                           fts_sort_t sort, int seed, char **after, int fetch_aggregations,
                           int highlight, int highlight_context_size, int model,
                           const float *embedding, int embedding_size, int fuzzy) {

    if (embedding) {
        int model_embedding_size = database_fts_get_model_size(db, model);
        if (model_embedding_size != embedding_size) {
            LOG_WARNINGF("database_fts.c", "Received invalid embedding size for model %d: %d, expected %d",
                         model, embedding_size, model_embedding_size);
            return NULL;
        }
    }

    // The expanded query replaces the one that was typed everywhere below, so the excerpts are
    // marked from the spellings that actually matched
    char *expanded_query = fuzzy ? database_fts_fuzzy_expand(db, query) : NULL;
    if (expanded_query != NULL) {
        query = expanded_query;
    }

    char *path_where = path_where_clause(paths);

    char **path_globs = NULL;
    if (path_where) {
        path_globs = calloc(array_length(paths), sizeof(char *));
        array_foreach(paths) {
            ASPRINTF_OR_FATAL(&path_globs[i], "%s/*", paths[i]);
        }
    }
    const char *size_where = size_where_clause(size_min, size_max);
    const char *date_where = date_where_clause(date_min, date_max);
    char *index_id_where = index_ids_where_clause(index_ids);
    char *mime_where = mime_types_where_clause(mime_types);
    const char *query_where = match_where(query);
    const char *after_where = get_after_where(after, sort, sort_asc);
    const char *tags_where = tags_where_clause(tags);

    if (!query_where && sort == FTS_SORT_SCORE) {
        // If query is NULL, then sort by id instead
        sort = FTS_SORT_ID;
    }

    char *agg_where = NULL;
    char *where = build_where_clause(path_where, size_where, date_where, index_id_where, mime_where, query_where,
                                     after_where, tags_where);
    if (fetch_aggregations) {
        agg_where = build_where_clause(path_where, size_where, date_where, index_id_where, mime_where, query_where,
                                       NULL, tags_where);
    }

    const char *json_object_sql = "json_set(doc.json_data,"
                                  "'$._id', CAST(doc.id AS TEXT),"
                                  "'$.index', doc.index_id,"
                                  "'$.thumbnail', doc.thumbnail_count,"
                                  "'$.mime', doc.mime,"
                                  "'$.size', doc.size,"
                                  // EXISTS, not a join: a document with several embeddings would
                                  // otherwise be returned once per embedding row
                                  "'$.embedding', EXISTS (SELECT 1 FROM embedding WHERE id = doc.id))";

    char *sql;
    char *agg_sql = NULL;

    // FTS5 only applies its top-N ranking optimisation to exactly `ORDER BY rank
    // LIMIT n`. Wrapping rank in round(), or adding the ROWID tiebreaker, makes
    // SQLite score and sort every match instead: 6.5s versus 0.7s for a term that
    // hits half of a 517k document index. So the ranking runs in a subquery that
    // keeps that exact shape, and everything expensive per row — the JSON, the
    // embedding lookup — happens for the N rows it returns.
    const int ranked = (sort == FTS_SORT_SCORE && query_where != NULL && sort_asc);

    if (ranked) {
        const char *ranked_after = get_ranked_after_where(after);
        char *ranked_where = build_where_clause(path_where, size_where, date_where, index_id_where,
                                                mime_where, query_where, ranked_after, tags_where);

        ASPRINTF_OR_FATAL(
                &sql,
                "SELECT"
                // %!.20g, not %g: SQLite caps %g at 16 significant digits whatever
                // precision is asked for, and a cursor that does not round-trip
                // exactly makes the next page repeat or skip rows.
                " %s, format('%%!.20g', top.rank_var) as sort_var, doc.ROWID"
                " FROM (SELECT search.ROWID as sid, rank as rank_var"
                "        FROM search"
                "        INNER JOIN document_index doc on doc.ROWID = search.ROWID"
                "        WHERE %s"
                "        ORDER BY rank"
                "        LIMIT ?2) top"
                " INNER JOIN document_index doc on doc.ROWID = top.sid"
                " ORDER BY top.rank_var, doc.ROWID",
                json_object_sql,
                ranked_where);

        free(ranked_where);

        if (fetch_aggregations) {
            ASPRINTF_OR_FATAL(&agg_sql,
                              "SELECT count(*), sum(size)"
                              " FROM search"
                              "  INNER JOIN document_index doc on doc.ROWID = search.ROWID"
                              " WHERE search MATCH ?1"
                              " AND %s", agg_where);
        }
    } else if (query_where) {
        ASPRINTF_OR_FATAL(
                &sql,
                "SELECT"
                " %s, %s as sort_var, doc.ROWID"
                " FROM search"
                " INNER JOIN document_index doc on doc.ROWID = search.ROWID"
                " WHERE %s"
                " ORDER BY sort_var%s, doc.ROWID%s"
                " LIMIT ?2",
                json_object_sql, get_sort_var(sort),
                where,
                sort_asc ? "" : " DESC", sort_asc ? "" : " DESC");

        if (fetch_aggregations) {
            ASPRINTF_OR_FATAL(&agg_sql,
                              "SELECT count(*), sum(size)"
                              " FROM search"
                              "  INNER JOIN document_index doc on doc.ROWID = search.ROWID"
                              " WHERE search MATCH ?1"
                              " AND %s", agg_where);
        }
    } else {
        ASPRINTF_OR_FATAL(
                &sql,
                "SELECT"
                " %s, %s as sort_var, doc.ROWID"
                " FROM document_index doc"
                " WHERE %s"
                " ORDER BY sort_var%s, doc.ROWID%s"
                " LIMIT ?2",
                json_object_sql, get_sort_var(sort),
                where,
                sort_asc ? "" : " DESC", sort_asc ? "" : " DESC");

        if (fetch_aggregations) {
            ASPRINTF_OR_FATAL(&agg_sql,
                              "SELECT count(*), sum(size)"
                              " FROM document_index doc"
                              " WHERE %s", agg_where);
        }
    }

    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL));

    if (query_where) {
        sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, 2, page_size);

    if (index_ids) {
        array_foreach(index_ids) {
            sqlite3_bind_int(stmt, INDEX_ID_PARAM_OFFSET + i, index_ids[i]);
        }
    }
    if (mime_types) {
        array_foreach(mime_types) {
            sqlite3_bind_text(stmt, MIME_PARAM_OFFSET + i, mime_types[i], -1, SQLITE_STATIC);
        }
    }
    if (tags) {
        db->tag_array = tags;
    }
    if (size_min > 0) {
        sqlite3_bind_int64(stmt, sqlite3_bind_parameter_index(stmt, "@size_min"), size_min);
    }
    if (size_max > 0) {
        sqlite3_bind_int64(stmt, sqlite3_bind_parameter_index(stmt, "@size_max"), size_max);
    }
    if (date_min > 0) {
        sqlite3_bind_int64(stmt, sqlite3_bind_parameter_index(stmt, "@date_min"), date_min);
    }
    if (date_max > 0) {
        sqlite3_bind_int64(stmt, sqlite3_bind_parameter_index(stmt, "@date_max"), date_max);
    }
    if (path_where) {
        array_foreach(paths) {
            sqlite3_bind_text(stmt, PATH_PARAM_OFFSET + i * 2, paths[i], -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, PATH_PARAM_OFFSET + i * 2 + 1, path_globs[i], -1, SQLITE_STATIC);
        }
    }
    if (after_where) {
        if (sort == FTS_SORT_NAME || sort == FTS_SORT_ID) {
            sqlite3_bind_text(stmt, 3, after[0], -1, SQLITE_STATIC);
        } else if (sort == FTS_SORT_SCORE || sort == FTS_SORT_EMBEDDING) {
            sqlite3_bind_double(stmt, 3, strtod(after[0], NULL));
        } else {
            sqlite3_bind_int64(stmt, 3, strtoll(after[0], NULL, 10));
        }
        // The cursor's tiebreaker is a document id, which is wider than a long on Windows
        sqlite3_bind_int64(stmt, 4, strtoll(after[1], NULL, 10));
    }
    if (sort == FTS_SORT_RANDOM) {
        sqlite3_bind_int(stmt, 5, seed);
    }
    if (embedding) {
        sqlite3_bind_int(stmt, 7, embedding_size);
        sqlite3_bind_blob(stmt, 8, embedding, (int) sizeof(float) * embedding_size, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 9, model);
    }

    char **terms = (highlight && query_where != NULL) ? highlight_query_terms(query) : NULL;

    // An embeddings search shows the chunk that matched rather than the head of the document, and
    // is worth a query per document of the page to find out which one it was
    sqlite3_stmt *chunk_stmt = NULL;
    if (highlight && sort == FTS_SORT_EMBEDDING && embedding != NULL) {
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                db->db,
                "SELECT start, end FROM embedding WHERE id = ?1 AND model_id = ?2"
                " ORDER BY cosine_sim(?3, ?4, embedding) DESC LIMIT 1", -1, &chunk_stmt, NULL));

        sqlite3_bind_int(chunk_stmt, 2, model);
        sqlite3_bind_int(chunk_stmt, 3, embedding_size);
        sqlite3_bind_blob(chunk_stmt, 4, embedding, (int) sizeof(float) * embedding_size, SQLITE_STATIC);
    }

    cJSON *json = cJSON_CreateObject();
    cJSON *hits_hits = cJSON_CreateArray();

    int ret;
    do {
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE && ret != SQLITE_ROW) {
            break;
        }

        if (ret == SQLITE_DONE) {
            break;
        }

        const char *json_str = (const char *) sqlite3_column_text(stmt, 0);
        cJSON *row = cJSON_CreateObject();
        cJSON *source = cJSON_Parse(json_str);
        if (terms != NULL || chunk_stmt != NULL) {
            const long long doc_id = sqlite3_column_int64(stmt, 2);

            long long chunk_start = -1;
            long long chunk_end = -1;
            if (chunk_stmt != NULL) {
                best_chunk(chunk_stmt, doc_id, &chunk_start, &chunk_end);
            }

            add_highlight(row, source, doc_id, terms, highlight_context_size, chunk_start, chunk_end);
        }
        cJSON *id = cJSON_DetachItemFromObject(source, "_id");
        cJSON_AddItemToObject(row, "_id", id);
        cJSON_AddItemToObject(row, "_source", source);

        cJSON *sort_info = cJSON_AddArrayToObject(row, "sort");
        cJSON_AddItemToArray(
                sort_info,
                cJSON_CreateString((char *) sqlite3_column_text(stmt, 1))
        );
        cJSON_AddItemToArray(
                sort_info,
                cJSON_CreateString((char *) sqlite3_column_text(stmt, 2))
        );

        cJSON_AddItemToArray(hits_hits, row);
    } while (TRUE);

    sqlite3_finalize(stmt);
    highlight_free_terms(terms);

    if (chunk_stmt != NULL) {
        sqlite3_finalize(chunk_stmt);
    }

    cJSON *hits = cJSON_AddObjectToObject(json, "hits");
    cJSON_AddItemToObject(hits, "hits", hits_hits);

    // Aggregations
    if (fetch_aggregations) {

        sqlite3_stmt *agg_stmt;
        CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(db->db, agg_sql, -1, &agg_stmt, NULL));

        if (index_ids) {
            array_foreach(index_ids) {
                sqlite3_bind_int(agg_stmt, INDEX_ID_PARAM_OFFSET + i, index_ids[i]);
            }
        }
        if (mime_types) {
            array_foreach(mime_types) {
                sqlite3_bind_text(agg_stmt, MIME_PARAM_OFFSET + i, mime_types[i], -1, SQLITE_STATIC);
            }
        }

        if (query_where) {
            sqlite3_bind_text(agg_stmt, 1, query, -1, SQLITE_STATIC);
        }
        if (size_min > 0) {
            sqlite3_bind_int64(agg_stmt, sqlite3_bind_parameter_index(agg_stmt, "@size_min"), size_min);
        }
        if (size_max > 0) {
            sqlite3_bind_int64(agg_stmt, sqlite3_bind_parameter_index(agg_stmt, "@size_max"), size_max);
        }
        if (date_min > 0) {
            sqlite3_bind_int64(agg_stmt, sqlite3_bind_parameter_index(agg_stmt, "@date_min"), date_min);
        }
        if (date_max > 0) {
            sqlite3_bind_int64(agg_stmt, sqlite3_bind_parameter_index(agg_stmt, "@date_max"), date_max);
        }
        if (path_where) {
            array_foreach(paths) {
                sqlite3_bind_text(agg_stmt, PATH_PARAM_OFFSET + i * 2, paths[i], -1, SQLITE_STATIC);
                sqlite3_bind_text(agg_stmt, PATH_PARAM_OFFSET + i * 2 + 1, path_globs[i], -1, SQLITE_STATIC);
            }
        }

        int agg_ret = sqlite3_step(agg_stmt);

        if (agg_ret == SQLITE_ROW) {
            cJSON *aggregations = cJSON_AddObjectToObject(json, "aggregations");
            cJSON *total_count = cJSON_AddObjectToObject(aggregations, "total_count");
            cJSON_AddNumberToObject(total_count, "value", sqlite3_column_double(agg_stmt, 0));
            cJSON *total_size = cJSON_AddObjectToObject(aggregations, "total_size");
            cJSON_AddNumberToObject(total_size, "value", sqlite3_column_double(agg_stmt, 1));
        } else {
            cJSON *aggregations = cJSON_AddObjectToObject(json, "aggregations");
            cJSON *total_count = cJSON_AddObjectToObject(aggregations, "total_count");
            cJSON_AddNumberToObject(total_count, "value", 0);
            cJSON *total_size = cJSON_AddObjectToObject(aggregations, "total_size");
            cJSON_AddNumberToObject(total_size, "value", 0);
        }
        sqlite3_finalize(agg_stmt);
    }

    // Cleanup
    if (path_where) {
        array_foreach(paths) { free(path_globs[i]); }
        free(path_globs);
        free(path_where);
    }
    if (index_id_where) {
        free(index_id_where);
    }
    if (mime_where) {
        free(mime_where);
    }
    free(where);
    free(sql);
    if (expanded_query) {
        free(expanded_query);
    }
    if (fetch_aggregations) {
        free(agg_where);
        free(agg_sql);
    }

    return json;
}

void database_fts_sync_tags(database_t *db) {

    LOG_INFO("database_fts.c", "Syncing tags.");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM fts.tag WHERE"
            " (id, index_id, tag) NOT IN (SELECT ((SELECT id FROM descriptor) << 32) | id, (SELECT id FROM descriptor), tag FROM tag)"
            " AND index_id = (SELECT id FROM descriptor)",
            NULL, NULL, NULL));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "INSERT INTO fts.tag (id, index_id, tag) "
            " SELECT (((SELECT id FROM descriptor) << 32) | id) as sid, (SELECT id FROM descriptor), tag FROM tag "
            " WHERE (sid, tag) NOT IN (SELECT id, tag FROM fts.tag)",
            NULL, NULL, NULL));
}

/**
 * The document, with the text read back from the index database it came from: the search index is
 * contentless, so document_index carries every field except .content.
 */
cJSON *database_fts_get_document(database_t *db, int64_t sid) {
    sqlite3_bind_int64(db->fts_get_document, 1, sid);

    int ret = sqlite3_step(db->fts_get_document);
    cJSON *json = NULL;
    int index_id = -1;

    if (ret == SQLITE_ROW) {
        const char *json_data = (const char *) sqlite3_column_text(db->fts_get_document, 0);
        json = cJSON_Parse(json_data);
        index_id = sqlite3_column_int(db->fts_get_document, 1);
    } else {
        CRASH_IF_STMT_FAIL(ret);
    }

    sqlite3_reset(db->fts_get_document);

    if (json == NULL) {
        return NULL;
    }

    database_t *index_db = index_database(index_id);

    if (index_db != NULL) {
        char *content = database_get_content(index_db, (int) (sid & 0xFFFFFFFF));

        if (content != NULL) {
            cJSON_AddStringToObject(json, "content", content);
            free(content);
        }
    }

    return json;
}

cJSON *database_fts_suggest_tag(database_t *db, char *prefix) {
    sqlite3_bind_text(db->fts_suggest_tag, 1, prefix, -1, NULL);

    cJSON *json = cJSON_CreateArray();

    int ret;
    do {
        ret = sqlite3_step(db->fts_suggest_tag);
        CRASH_IF_STMT_FAIL(ret);

        if (ret == SQLITE_DONE) {
            break;
        }

        cJSON_AddItemToArray(
                json,
                cJSON_CreateString((const char *) sqlite3_column_text(db->fts_suggest_tag, 0))
        );

    } while (TRUE);

    sqlite3_reset(db->fts_suggest_tag);

    return json;
}


cJSON *database_fts_get_tags(database_t *db) {
    cJSON *json = cJSON_CreateArray();

    int ret;
    do {
        ret = sqlite3_step(db->fts_get_tags);
        CRASH_IF_STMT_FAIL(ret);

        if (ret == SQLITE_DONE) {
            break;
        }

        cJSON *row = cJSON_CreateObject();

        cJSON_AddStringToObject(row, "tag", (const char *) sqlite3_column_text(db->fts_get_tags, 0));
        cJSON_AddNumberToObject(row, "count", sqlite3_column_int(db->fts_get_tags, 1));

        cJSON_AddItemToArray(json, row);
    } while (TRUE);

    sqlite3_reset(db->fts_get_tags);

    return json;
}
void database_fts_write_tag(database_t *db, int64_t sid, char *tag) {
    sqlite3_bind_int64(db->fts_write_tag_stmt, 1, sid);
    sqlite3_bind_int(db->fts_write_tag_stmt, 2, (int) (sid >> 32));
    sqlite3_bind_text(db->fts_write_tag_stmt, 3, tag, -1, SQLITE_STATIC);

    CRASH_IF_STMT_FAIL(sqlite3_step(db->fts_write_tag_stmt));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->fts_write_tag_stmt));
}
