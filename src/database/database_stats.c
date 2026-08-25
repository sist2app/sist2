#include "database.h"
#include "src/sist.h"
#include "src/ctx.h"

#define TREEMAP_MINIMUM_MERGES_TO_CONTINUE (100)
#define SIZE_BUCKET (int64_t)(5 * 1000 * 1000)
#define DATE_BUCKET (int64_t)(2629800) // ~30 days


database_iterator_t *database_create_treemap_iterator(database_t *db, int64_t threshold) {

    sqlite3_stmt *stmt;

    sqlite3_prepare_v2(db->db,
                       "SELECT path, path_parent(path), size FROM tm"
                       " WHERE path_parent(path) IN (SELECT path FROM tm)"
                       " AND size<?",
                       -1, &stmt, NULL);

    sqlite3_bind_int64(stmt, 1, threshold);

    database_iterator_t *iter = malloc(sizeof(database_iterator_t));

    iter->stmt = stmt;
    iter->db = db;

    return iter;
}

treemap_row_t database_treemap_iter(database_iterator_t *iter) {

    if (iter->stmt == NULL) {
        LOG_FATAL("database.c", "FIXME: database_treemap_iter() called after iteration stopped");
    }

    int ret = sqlite3_step(iter->stmt);

    if (ret == SQLITE_ROW) {
        treemap_row_t row = {
                .path = (const char *) sqlite3_column_text(iter->stmt, 0),
                .parent = (const char *) sqlite3_column_text(iter->stmt, 1),
                .size = sqlite3_column_int64(iter->stmt, 2)
        };

        return row;
    }

    if (ret != SQLITE_DONE) {
        LOG_FATALF("database.c", "FIXME: doc iter returned %s", sqlite3_errmsg(iter->db->db));
    }

    sqlite3_finalize(iter->stmt);
    iter->stmt = NULL;

    return (treemap_row_t) {NULL, NULL, 0};
}

void database_generate_stats(database_t *db, double treemap_threshold) {
    database_flush_writes(db);

    // An index database created by an older version does not have it
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db,
            "CREATE INDEX IF NOT EXISTS document_stats_idx ON document(parent, path, size, mtime, mime);",
            NULL, NULL, NULL));

    LOG_INFO("database.c", "Generating stats");

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DELETE FROM stats_size_agg;", NULL, NULL, NULL));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DELETE FROM stats_date_agg;", NULL, NULL, NULL));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DELETE FROM stats_mime_agg;", NULL, NULL, NULL));
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DELETE FROM stats_treemap;", NULL, NULL, NULL));

    CRASH_IF_NOT_SQLITE_OK(
            sqlite3_exec(db->db, "CREATE TEMP TABLE tm(path TEXT PRIMARY KEY, size INT);", NULL, NULL, NULL));

    // The aggregates below group the same rows four different ways. Reading the three columns they
    // need once, into a table small enough to stay in memory, turns four scans of the document
    // index into one — the scans are what the whole step costs, and they are read from disk.
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(
            db->db, "CREATE TEMP TABLE doc_summary AS SELECT size, mtime, mime FROM document;",
            NULL, NULL, NULL));

    sqlite3_prepare_v2(db->db, "UPDATE tm SET size=size+? WHERE path=?;", -1, &db->treemap_merge_up_update_stmt, NULL);
    sqlite3_prepare_v2(db->db, "DELETE FROM tm WHERE path = ?;", -1, &db->treemap_merge_up_delete_stmt, NULL);

    // size aggregation
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db->db, "INSERT INTO stats_size_agg"
                               " SELECT"
                               "  cast(size / ?1 as int) * ?1 as bucket,"
                               "  count(*) as count"
                               " FROM doc_summary"
                               " GROUP BY bucket", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, SIZE_BUCKET);
    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));

    sqlite3_finalize(stmt);

    // date aggregation
    sqlite3_prepare_v2(db->db, "INSERT INTO stats_date_agg"
                               " SELECT"
                               "  cast(mtime / ?1 as int) * ?1 as bucket,"
                               "  count(*) as count"
                               " FROM doc_summary"
                               " GROUP BY bucket", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, DATE_BUCKET);
    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));

    sqlite3_finalize(stmt);

    // mime aggregation
    sqlite3_prepare_v2(db->db, "INSERT INTO stats_mime_agg"
                               " SELECT"
                               "  m.name as bucket,"
                               "  sum(size),"
                               "  count(*)"
                               " FROM doc_summary INNER JOIN mime m ON m.id=doc_summary.mime"
                               " WHERE bucket IS NOT NULL"
                               " GROUP BY bucket", -1, &stmt, NULL);
    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));

    sqlite3_finalize(stmt);

    // Treemap
    sqlite3_prepare_v2(db->db, "SELECT SUM(size) FROM doc_summary;", -1, &stmt, NULL);
    CRASH_IF_STMT_FAIL(sqlite3_step(stmt));
    int64_t total_size = sqlite3_column_int64(stmt, 0);
    int64_t threshold = (int64_t) ((double) total_size * treemap_threshold);
    sqlite3_finalize(stmt);

    // flat map
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db,
                                        "INSERT INTO tm (path, size) SELECT path, sum(size)"
                                        " FROM document WHERE parent IS NULL GROUP BY path;",
                                        NULL, NULL, NULL));

    // Merge up
    sqlite3_prepare_v2(db->db,
                       "INSERT INTO tm (path, size) SELECT path_parent(path) as parent, 0 "
                       " FROM tm WHERE parent not IN (SELECT path FROM tm) AND size<?"
                       " ON CONFLICT DO NOTHING;", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, threshold);

    int merged_rows = 0;
    do {
        if (merged_rows) {
            LOG_INFOF("database.c", "Treemap merge iteration (%d rows changed)", merged_rows);
        }
        merged_rows = 0;

        CRASH_IF_STMT_FAIL(sqlite3_step(stmt));
        CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(stmt));

        database_iterator_t *iter = database_create_treemap_iterator(db, threshold);
        database_treemap_iter_foreach(row, iter) {
            sqlite3_bind_int64(db->treemap_merge_up_update_stmt, 1, row.size);
            sqlite3_bind_text(db->treemap_merge_up_update_stmt, 2, row.parent, -1, SQLITE_STATIC);
            CRASH_IF_STMT_FAIL(sqlite3_step(db->treemap_merge_up_update_stmt));
            CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->treemap_merge_up_update_stmt));

            sqlite3_bind_text(db->treemap_merge_up_delete_stmt, 1, row.path, -1, SQLITE_STATIC);
            CRASH_IF_STMT_FAIL(sqlite3_step(db->treemap_merge_up_delete_stmt));
            CRASH_IF_NOT_SQLITE_OK(sqlite3_reset(db->treemap_merge_up_delete_stmt));

            merged_rows += 1;
        }
        free(iter);
    } while (merged_rows > TREEMAP_MINIMUM_MERGES_TO_CONTINUE);

    sqlite3_finalize(stmt);

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db,
                                        "INSERT INTO stats_treemap (path, size) SELECT path,size FROM tm;",
                                        NULL, NULL, NULL));

    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, "DROP TABLE doc_summary;", NULL, NULL, NULL));

    LOG_INFO("database.c", "Done!");
}

database_stat_type_d database_get_stat_type_by_mnemonic(const char *name) {
    if (strcmp(name, "TMAP") == 0) {
        return DATABASE_STAT_TREEMAP;
    }
    if (strcmp(name, "MAGG") == 0) {
        return DATABASE_STAT_MIME_AGG;
    }
    if (strcmp(name, "SAGG") == 0) {
        return DATABASE_STAT_SIZE_AGG;
    }
    if (strcmp(name, "DAGG") == 0) {
        return DATABASE_STAT_DATE_AGG;
    }

    return DATABASE_STAT_INVALID;
}

cJSON *database_get_stats(database_t *db, database_stat_type_d type) {

    sqlite3_stmt *stmt;

    switch (type) {
        case DATABASE_STAT_TREEMAP:
            CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                    db->db, "SELECT path,size FROM stats_treemap", -1, &stmt, NULL
            ));
            break;
        case DATABASE_STAT_DATE_AGG:
            CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                    db->db, "SELECT bucket,count FROM stats_date_agg", -1, &stmt, NULL
            ));
            break;
        case DATABASE_STAT_SIZE_AGG:
            CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                    db->db, "SELECT bucket,count FROM stats_size_agg", -1, &stmt, NULL
            ));
            break;
        case DATABASE_STAT_MIME_AGG:
            CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
                    db->db, "SELECT mime,size,count FROM stats_mime_agg", -1, &stmt, NULL
            ));
            break;
        case DATABASE_STAT_INVALID:
        default:
        LOG_FATALF("database_stats.c", "Invalid stat type: %d", type);
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

        switch (type) {
            case DATABASE_STAT_TREEMAP:
                cJSON_AddStringToObject(row, "path", (const char *) sqlite3_column_text(stmt, 0));
                cJSON_AddNumberToObject(row, "size", (double) sqlite3_column_int64(stmt, 1));
                break;
            case DATABASE_STAT_DATE_AGG:
            case DATABASE_STAT_SIZE_AGG:
                cJSON_AddNumberToObject(row, "bucket", (double) sqlite3_column_int64(stmt, 0));
                cJSON_AddNumberToObject(row, "count", (double) sqlite3_column_int64(stmt, 1));
                break;
            case DATABASE_STAT_MIME_AGG:
                cJSON_AddStringToObject(row, "mime", (const char *) sqlite3_column_text(stmt, 0));
                cJSON_AddNumberToObject(row, "size", (double) sqlite3_column_int64(stmt, 1));
                cJSON_AddNumberToObject(row, "count", (double) sqlite3_column_int64(stmt, 2));
                break;
            case DATABASE_STAT_INVALID:
                LOG_FATAL("database_stats.c", "Invalid stat type");
        }

        cJSON_AddItemToArray(json, row);
    } while (TRUE);

    sqlite3_finalize(stmt);

    return json;
}