#include "database.h"
#include "src/ctx.h"

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

void database_fts_index(database_t *db) {

    LOG_INFO("database_fts.c", "Creating content table");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
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
            "  document.json_data"
            " FROM document"
            " LEFT JOIN mime m ON m.id=document.mime"
            " )"
            " INSERT"
            " INTO fts.document_index (id, index_id, size, name, path, mtime, mime, thumbnail_count, json_data)"
            " SELECT * FROM docs WHERE true"
            " on conflict (id) do update set "
            "  size=excluded.size, mtime=excluded.mtime, mime=excluded.mime, json_data=excluded.json_data;",
            NULL, NULL, NULL));

    LOG_DEBUG("database_fts.c", "Copying embeddings");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "REPLACE INTO fts.model (id, size)"
            " SELECT id, size FROM model", NULL, NULL, NULL));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "REPLACE INTO fts.embedding (id, model_id, start, end, embedding)"
            " SELECT (SELECT id FROM descriptor) << 32 | id, model_id, start, end, embedding FROM embedding "
            " WHERE TRUE ON CONFLICT (id, model_id, start) DO NOTHING;", NULL, NULL, NULL));

    // TODO: delete old embeddings

    LOG_DEBUG("database_fts.c", "Deleting old documents");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM fts.document_index"
            " WHERE id IN (SELECT id FROM delete_list)"
            "  AND index_id = (SELECT id FROM descriptor);",
            NULL, NULL, NULL));

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
            " SELECT path, index_id, count(*), CASE WHEN length(json_data->>'path') == 0 THEN 0"
            " ELSE 1 + length(json_data->>'path') - length(REPLACE(json_data->>'path', '/', ''))"
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
    }

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "DELETE FROM path_index;"
            "INSERT INTO path_index (path, index_id, count, depth) SELECT path, index_id, total, depth FROM path_tmp",
            NULL, NULL, NULL));

    LOG_DEBUG("database_fts.c", "Generating search index");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db, "INSERT INTO search(search) VALUES ('delete-all')",
            NULL, NULL, NULL));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "INSERT INTO search(rowid, name, content, title, path) "
            "SELECT id, name, content, title, path from document_view",
            NULL, NULL, NULL));
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

const char *size_where_clause(long size_min, long size_max) {
    if (size_min > 0 && size_max > 0) {
        return "size BETWEEN @size_min AND @size_max";
    } else if (size_min > 0) {
        return "size >= @size_min";
    } else if (size_max > 0) {
        return "size <= @size_max";
    }

    return NULL;
}

const char *date_where_clause(long date_min, long date_max) {
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

const char *path_where_clause(const char *path) {
    if (path == NULL || strlen(path) == 0) {
        return NULL;
    }

    // Qualified: the fts5 search table also has a path column, and an unqualified
    // reference is ambiguous in every query that joins the two.
    return "(doc.path = @path or doc.path GLOB @path_glob)";
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
            return "cosine_sim(?7, ?8, emb.embedding)";
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

char *get_after_where(char **after, UNUSED(fts_sort_t sort), int sort_asc) {
    if (after == NULL) {
        return NULL;
    }

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

cJSON *database_fts_search(database_t *db, const char *query, const char *path, long size_min,
                           long size_max, long date_min, long date_max, int page_size,
                           int *index_ids, char **mime_types, char **tags, int sort_asc,
                           fts_sort_t sort, int seed, char **after, int fetch_aggregations,
                           int highlight, int highlight_context_size, int model,
                           const float *embedding, int embedding_size) {

    if (embedding) {
        int model_embedding_size = database_fts_get_model_size(db, model);
        if (model_embedding_size != embedding_size) {
            LOG_WARNINGF("database_fts.c", "Received invalid embedding size for model %s: %d, expected %d",
                         model, embedding_size, model_embedding_size);
            return NULL;
        }
    }

    const char *path_where = path_where_clause(path);

    char path_glob[PATH_MAX * 2] = {0};
    if (path_where) {
        snprintf(path_glob, sizeof(path_glob), "%s/*", path);
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

    char *agg_where;
    char *where = build_where_clause(path_where, size_where, date_where, index_id_where, mime_where, query_where,
                                     after_where, tags_where);
    if (fetch_aggregations) {
        agg_where = build_where_clause(path_where, size_where, date_where, index_id_where, mime_where, query_where,
                                       NULL, tags_where);
    }

    const char *json_object_sql;
    if (highlight && query_where != NULL) {
        json_object_sql = "json_set(json_remove(doc.json_data, '$.content'),"
                          "'$._id', CAST(doc.id AS TEXT),"
                          "'$.index', doc.index_id,"
                          "'$.thumbnail', doc.thumbnail_count,"
                          "'$.mime', doc.mime,"
                          "'$.size', doc.size,"
                          "'$.embedding', (CASE WHEN emb.id IS NOT NULL THEN 1 ELSE 0 END),"
                          "'$._highlight.name', snippet(search, 0, '<mark>', '</mark>', '', ?6),"
                          "'$._highlight.content', snippet(search, 1, '<mark>', '</mark>', '', ?6))";
    } else {
        json_object_sql = "json_set(json_remove(doc.json_data, '$.content'),"
                          "'$._id', CAST(doc.id AS TEXT),"
                          "'$.index', doc.index_id,"
                          "'$.thumbnail', doc.thumbnail_count,"
                          "'$.mime', doc.mime,"
                          "'$.size', doc.size,"
                          "'$.embedding', (CASE WHEN emb.id IS NOT NULL THEN 1 ELSE 0 END))";
    }

    char *sql;
    char *agg_sql;

    if (query_where) {
        asprintf(
                &sql,
                "SELECT"
                " %s, %s as sort_var, doc.ROWID"
                " FROM search"
                " INNER JOIN document_index doc on doc.ROWID = search.ROWID"
                " LEFT JOIN embedding emb on emb.id = doc.id"
                " WHERE %s"
                " ORDER BY sort_var%s, doc.ROWID"
                " LIMIT ?2",
                json_object_sql, get_sort_var(sort),
                where,
                sort_asc ? "" : " DESC");

        if (fetch_aggregations) {
            asprintf(&agg_sql,
                     "SELECT count(*), sum(size)"
                     " FROM search"
                     "  INNER JOIN document_index doc on doc.ROWID = search.ROWID"
                     " WHERE search MATCH ?1"
                     " AND %s", agg_where);
        }
    } else {
        asprintf(
                &sql,
                "SELECT"
                " %s, %s as sort_var, doc.ROWID"
                " FROM document_index doc"
                " LEFT JOIN embedding emb on emb.id = doc.id"
                " WHERE %s"
                " ORDER BY sort_var%s,doc.ROWID"
                " LIMIT ?2",
                json_object_sql, get_sort_var(sort),
                where,
                sort_asc ? "" : " DESC");

        if (fetch_aggregations) {
            asprintf(&agg_sql,
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
        sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, "@path"), path, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, "@path_glob"), path_glob, -1, SQLITE_STATIC);
    }
    if (after_where) {
        if (sort == FTS_SORT_NAME || sort == FTS_SORT_ID) {
            sqlite3_bind_text(stmt, 3, after[0], -1, SQLITE_STATIC);
        } else if (sort == FTS_SORT_SCORE || sort == FTS_SORT_EMBEDDING) {
            sqlite3_bind_double(stmt, 3, strtod(after[0], NULL));
        } else {
            sqlite3_bind_int64(stmt, 3, strtol(after[0], NULL, 10));
        }
        sqlite3_bind_int64(stmt, 4, strtol(after[1], NULL, 10));
    }
    if (sort == FTS_SORT_RANDOM) {
        sqlite3_bind_int(stmt, 5, seed);
    }
    if (highlight) {
        sqlite3_bind_int(stmt, 6, highlight_context_size);
    }
    if (embedding) {
        sqlite3_bind_int(stmt, 7, embedding_size);
        sqlite3_bind_blob(stmt, 8, embedding, (int) sizeof(float) * embedding_size, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 9, model);
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
        if (highlight) {
            cJSON *hl = cJSON_DetachItemFromObject(source, "_highlight");
            cJSON_AddItemToObject(row, "highlight", hl);
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
            sqlite3_bind_text(agg_stmt, sqlite3_bind_parameter_index(agg_stmt, "@path"), path, -1, SQLITE_STATIC);
            sqlite3_bind_text(agg_stmt, sqlite3_bind_parameter_index(agg_stmt, "@path_glob"), path_glob, -1,
                              SQLITE_STATIC);
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
    if (index_id_where) {
        free(index_id_where);
    }
    if (mime_where) {
        free(mime_where);
    }
    free(where);
    free(sql);
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

cJSON *database_fts_get_document(database_t *db, long sid) {
    sqlite3_bind_int64(db->fts_get_document, 1, sid);

    int ret = sqlite3_step(db->fts_get_document);
    cJSON *json = NULL;

    if (ret == SQLITE_ROW) {
        const char *json_data = (const char *) sqlite3_column_text(db->fts_get_document, 0);
        json = cJSON_Parse(json_data);
    } else {
        CRASH_IF_STMT_FAIL(ret);
    }

    sqlite3_reset(db->fts_get_document);

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
void database_fts_write_tag(database_t *db, long sid, char *tag) {
    sqlite3_bind_int64(db->fts_write_tag_stmt, 1, sid);
    sqlite3_bind_int(db->fts_write_tag_stmt, 2, (int) (sid >> 32));
    sqlite3_bind_text(db->fts_write_tag_stmt, 3, tag, -1, SQLITE_STATIC);

    CRASH_IF_STMT_FAIL(sqlite3_step(db->fts_write_tag_stmt));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->fts_write_tag_stmt));
}
