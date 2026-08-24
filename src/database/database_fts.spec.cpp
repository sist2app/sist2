#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>

extern "C" {
#include "src/database/database.h"
#include "src/ctx.h"
}

/*
 * End-to-end tests of the search index build: they run the real `sist2 scan` and
 * `sist2 sqlite-index` binaries and check what a query against the resulting index returns.
 */

namespace fs = std::filesystem;

class SqliteIndexTest : public ::testing::Test {
protected:
    fs::path dir;
    fs::path index;
    fs::path search_index;

    void SetUp() override {
        dir = fs::temp_directory_path() / ("sist2-fts-spec-" + std::to_string(getpid()));
        index = dir / "index.sist2";
        search_index = dir / "search.sist2";

        fs::remove_all(dir);
        fs::create_directories(dir / "files");

        for (int i = 0; i < 8; i++) {
            write_file("file-" + std::to_string(i) + ".txt", "alpha zebra document number " + std::to_string(i));
        }
    }

    void TearDown() override {
        fs::remove_all(dir);
    }

    void write_file(const std::string &name, const std::string &content) {
        fs::create_directories((dir / "files" / name).parent_path());

        std::ofstream file(dir / "files" / name);
        file << content << "\n";
        file.close();

        // Modified files are picked up by mtime, and a test writes them within the same second
        fs::last_write_time(dir / "files" / name,
                            fs::file_time_type::clock::now() + std::chrono::hours(24 * ++mtime_offset));
    }

    int scan(const std::string &extra_args = "") {
        return run(std::string(SIST2_BINARY) + " scan --threads 4 " + extra_args
                   + " -o " + index.string() + " " + (dir / "files").string());
    }

    int sqlite_index(const std::string &extra_args = "") {
        return run(std::string(SIST2_BINARY) + " sqlite-index --search-index " + search_index.string()
                   + " " + extra_args + " " + index.string());
    }

    static int run(const std::string &command) {
        const int status = system((command + " > /dev/null 2>&1").c_str());
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    long long query(const std::string &sql) {
        sqlite3 *db;
        if (sqlite3_open(search_index.c_str(), &db) != SQLITE_OK) {
            return -1;
        }

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            return -1;
        }

        const long long result = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int64(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return result;
    }

    bool exec(const std::string &sql) {
        sqlite3 *db;
        if (sqlite3_open(search_index.c_str(), &db) != SQLITE_OK) {
            return false;
        }

        const bool ok = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
        sqlite3_close(db);

        return ok;
    }

    long long matches(const std::string &term) {
        return query("SELECT count(*) FROM search WHERE search MATCH '" + term + "'");
    }

    // Returns false if fts5 finds the search index inconsistent with its content table
    bool integrity_ok() {
        return exec("INSERT INTO search(search) VALUES('integrity-check')");
    }

    static std::string blob_literal(const std::vector<float> &values) {
        std::string hex = "X'";
        for (const float value: values) {
            unsigned char bytes[sizeof(float)];
            memcpy(bytes, &value, sizeof(float));
            for (const unsigned char byte: bytes) {
                char buf[3];
                snprintf(buf, sizeof(buf), "%02x", byte);
                hex += buf;
            }
        }
        return hex + "'";
    }

    /** Every document id of the search index, in ascending order */
    std::vector<long long> document_ids() {
        std::vector<long long> ids;

        sqlite3 *db;
        if (sqlite3_open(search_index.c_str(), &db) != SQLITE_OK) {
            return ids;
        }

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, "SELECT id FROM document_index ORDER BY id", -1, &stmt,
                               nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                ids.push_back(sqlite3_column_int64(stmt, 0));
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_close(db);
        return ids;
    }

    bool register_model(int model_id, int size) {
        return exec("INSERT INTO model (id, size) VALUES (" + std::to_string(model_id) + ", "
                    + std::to_string(size) + ")");
    }

    bool add_embedding(long long doc_id, int model_id, int start, const std::vector<float> &values) {
        return exec("INSERT INTO embedding (id, model_id, start, end, embedding) VALUES ("
                    + std::to_string(doc_id) + ", " + std::to_string(model_id) + ", "
                    + std::to_string(start) + ", NULL, " + blob_literal(values) + ")");
    }

    struct Hit {
        std::string id;
        std::string sort_value;
        std::string sort_tiebreaker;
    };

    /** One page of results, with the cursor each hit would be paged after */
    std::vector<Hit> search(database_t *db, fts_sort_t sort, int sort_asc, int page_size,
                            char **after = nullptr, int model = 0,
                            const std::vector<float> &embedding = {}) {
        cJSON *result = database_fts_search(db, nullptr, nullptr, 0, 0, 0, 0, page_size, nullptr,
                                            nullptr, nullptr, sort_asc, sort, 0, after, FALSE,
                                            FALSE, 0, model,
                                            embedding.empty() ? nullptr : embedding.data(),
                                            (int) embedding.size());

        std::vector<Hit> hits;
        if (result == nullptr) {
            return hits;
        }

        const cJSON *rows = cJSON_GetObjectItem(cJSON_GetObjectItem(result, "hits"), "hits");
        const cJSON *row;
        cJSON_ArrayForEach(row, rows) {
            const cJSON *sort_info = cJSON_GetObjectItem(row, "sort");
            hits.push_back({
                cJSON_GetObjectItem(row, "_id")->valuestring,
                cJSON_GetArrayItem(sort_info, 0)->valuestring,
                cJSON_GetArrayItem(sort_info, 1)->valuestring
            });
        }

        cJSON_Delete(result);
        return hits;
    }

    /** Every id a client walking the pages with the `after` cursor would see */
    std::vector<std::string> page_through(database_t *db, fts_sort_t sort, int sort_asc,
                                          int page_size) {
        std::vector<std::string> ids;
        std::vector<Hit> page = search(db, sort, sort_asc, page_size);

        while (!page.empty() && ids.size() < 1000) {
            for (const Hit &hit: page) {
                ids.push_back(hit.id);
            }

            std::string value = page.back().sort_value;
            std::string tiebreaker = page.back().sort_tiebreaker;
            char *after[] = {value.data(), tiebreaker.data(), nullptr};

            page = search(db, sort, sort_asc, page_size, after);
        }

        return ids;
    }

    int mtime_offset = 0;
};

/** The folder tree of every index at once: no index id means no index filter, not no results */
TEST_F(SqliteIndexTest, PathsOfEveryIndexAtOnce) {
    write_file("sub/one.txt", "alpha");
    write_file("sub/deeper/two.txt", "alpha");

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const long long index_id = query("SELECT DISTINCT index_id FROM path_index");
    ASSERT_GT(index_id, 0);

    database_t *db = database_create(search_index.c_str(), FTS_DATABASE);
    database_open(db);

    cJSON *of_index = database_fts_get_paths(db, (int) index_id, 1, 3, nullptr, FALSE);
    cJSON *of_every_index = database_fts_get_paths(db, 0, 1, 3, nullptr, FALSE);

    EXPECT_EQ(cJSON_GetArraySize(of_index), 2);
    EXPECT_EQ(cJSON_GetArraySize(of_every_index), cJSON_GetArraySize(of_index));

    cJSON_Delete(of_index);
    cJSON_Delete(of_every_index);
    database_close(db, FALSE);
}

/*
 * A search carrying an embedding for a model the search index does not know used to take the
 * whole web server down with it: the mismatch was logged with %s and an int model id.
 */
TEST_F(SqliteIndexTest, EmbeddingForAnUnknownModelIsRejected) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    database_t *db = database_create(search_index.c_str(), FTS_DATABASE);
    database_open(db);

    // Every real run logs warnings; the mismatch is only formatted when they are on
    const int verbose = LogCtx.verbose;
    LogCtx.verbose = 1;

    std::vector<float> embedding(512, 0.5f);

    cJSON *result = database_fts_search(db, nullptr, nullptr, 0, 0, 0, 0, 10, nullptr, nullptr,
                                        nullptr, TRUE, FTS_SORT_SCORE, 0, nullptr, FALSE, FALSE,
                                        0, 1, embedding.data(), (int) embedding.size());

    EXPECT_EQ(result, nullptr);

    LogCtx.verbose = verbose;
    database_close(db, FALSE);
}

TEST_F(SqliteIndexTest, FullBuild) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(matches("alpha"), 8);
    EXPECT_EQ(query("SELECT count(*) FROM document_index"), 8);
    // The text lives in the index database, not in a second copy here
    EXPECT_EQ(query("SELECT count(*) FROM document_index WHERE json_data ->> 'content' IS NOT NULL"), 0);
    EXPECT_TRUE(integrity_ok());
}

TEST_F(SqliteIndexTest, IncrementalUpdate) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    write_file("file-0.txt", "alpha replaced with omega");
    write_file("new-file.txt", "brandnew content");
    fs::remove(dir / "files" / "file-1.txt");

    ASSERT_EQ(scan("--incremental"), 0);
    ASSERT_EQ(sqlite_index(), 0);

    // The updated document keeps the terms it still has, loses the ones it does not
    EXPECT_EQ(matches("alpha"), 7);
    EXPECT_EQ(matches("omega"), 1);
    EXPECT_EQ(matches("zebra"), 6);
    EXPECT_EQ(matches("brandnew"), 1);

    EXPECT_EQ(query("SELECT count(*) FROM document_index"), 8);
    EXPECT_EQ(query("SELECT documents FROM index_state"), 8);
    EXPECT_TRUE(integrity_ok());
}

TEST_F(SqliteIndexTest, IncrementalMatchesRebuild) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    write_file("file-0.txt", "alpha replaced with omega");
    fs::remove(dir / "files" / "file-1.txt");

    ASSERT_EQ(scan("--incremental"), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const std::vector<std::string> terms = {"alpha", "omega", "zebra", "document"};
    std::vector<long long> incremental;
    for (const auto &term: terms) {
        incremental.push_back(matches(term));
    }

    ASSERT_EQ(sqlite_index("--rebuild"), 0);

    for (size_t i = 0; i < terms.size(); i++) {
        EXPECT_EQ(matches(terms[i]), incremental[i]) << "term: " << terms[i];
    }
    EXPECT_TRUE(integrity_ok());
}

TEST_F(SqliteIndexTest, RunWithNoChangesIsANoop) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(matches("alpha"), 8);
    EXPECT_EQ(query("SELECT count(*) FROM document_index"), 8);
    EXPECT_TRUE(integrity_ok());
}

TEST_F(SqliteIndexTest, InterruptedRunIsRebuilt) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    // What a run that died halfway through leaves behind: the search index cannot be trusted, so the
    // next run has to rebuild it instead of applying changes on top
    ASSERT_TRUE(exec("UPDATE index_state SET dirty = 1;"
                     "DELETE FROM search WHERE rowid IN (SELECT id FROM document_index);"));
    ASSERT_EQ(matches("alpha"), 0);

    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(matches("alpha"), 8);
    EXPECT_TRUE(integrity_ok());
}

/*
 * A document carries one embedding per chunk of its content. The search used to join the embedding
 * table into the result set, so a chunked document was returned once per chunk.
 */
TEST_F(SqliteIndexTest, DocumentWithSeveralEmbeddingsIsReturnedOnce) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const std::vector<long long> ids = document_ids();
    ASSERT_EQ(ids.size(), 8);

    ASSERT_TRUE(register_model(1, 4));
    for (int start = 0; start < 3; start++) {
        ASSERT_TRUE(add_embedding(ids[0], 1, start, {1, 0, 0, 0}));
    }

    database_t *db = database_create(search_index.c_str(), FTS_DATABASE);
    database_open(db);

    const std::vector<Hit> hits = search(db, FTS_SORT_NAME, TRUE, 100);

    EXPECT_EQ(hits.size(), 8);

    std::set<std::string> unique;
    for (const Hit &hit: hits) {
        unique.insert(hit.id);
    }
    EXPECT_EQ(unique.size(), 8);

    database_close(db, FALSE);
}

/** A chunked document ranks by its best-matching chunk, not by an arbitrary one */
TEST_F(SqliteIndexTest, EmbeddingSortRanksByTheBestChunk) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const std::vector<long long> ids = document_ids();
    ASSERT_EQ(ids.size(), 8);

    const std::vector<float> query = {1, 0, 0, 0};

    ASSERT_TRUE(register_model(1, 4));
    // Its first chunk is a poor match and its second one is the query itself
    ASSERT_TRUE(add_embedding(ids[3], 1, 0, {0, 0, 0, 1}));
    ASSERT_TRUE(add_embedding(ids[3], 1, 1, query));
    // Every other document has one chunk, none of them as good
    for (size_t i = 0; i < ids.size(); i++) {
        if (i != 3) {
            ASSERT_TRUE(add_embedding(ids[i], 1, 0, {0.5f, 0.5f, 0, 0}));
        }
    }

    database_t *db = database_create(search_index.c_str(), FTS_DATABASE);
    database_open(db);

    const std::vector<Hit> hits = search(db, FTS_SORT_EMBEDDING, FALSE, 100, nullptr, 1, query);

    ASSERT_EQ(hits.size(), 8);
    EXPECT_EQ(hits[0].id, std::to_string(ids[3]));
    EXPECT_NEAR(strtod(hits[0].sort_value.c_str(), nullptr), 1.0, 1e-6);
    EXPECT_LT(strtod(hits[1].sort_value.c_str(), nullptr), 1.0);

    database_close(db, FALSE);
}

/** A document the model has no embedding for sorts last, and keeps a numeric cursor */
TEST_F(SqliteIndexTest, EmbeddingSortKeepsDocumentsWithoutAnEmbedding) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const std::vector<long long> ids = document_ids();
    const std::vector<float> query = {1, 0, 0, 0};

    ASSERT_TRUE(register_model(1, 4));
    ASSERT_TRUE(add_embedding(ids[0], 1, 0, query));

    database_t *db = database_create(search_index.c_str(), FTS_DATABASE);
    database_open(db);

    const std::vector<Hit> hits = search(db, FTS_SORT_EMBEDDING, FALSE, 100, nullptr, 1, query);

    ASSERT_EQ(hits.size(), 8);
    EXPECT_EQ(hits[0].id, std::to_string(ids[0]));
    for (size_t i = 1; i < hits.size(); i++) {
        EXPECT_EQ(strtod(hits[i].sort_value.c_str(), nullptr), -1) << "hit " << i;
    }

    database_close(db, FALSE);
}

/*
 * The `after` cursor compares (sort_var, ROWID) as one tuple, so the ROWID tiebreaker has to run in
 * the same direction as the sort: a descending sort whose documents share a sort value used to skip
 * every row of the tie but one.
 */
TEST_F(SqliteIndexTest, DescendingSortPagesThroughDocumentsThatTie) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    // Every file was written with the same length, so they all tie on size
    ASSERT_EQ(query("SELECT count(DISTINCT size) FROM document_index"), 1);

    database_t *db = database_create(search_index.c_str(), FTS_DATABASE);
    database_open(db);

    for (const int sort_asc: {1, 0}) {
        const std::vector<std::string> ids = page_through(db, FTS_SORT_SIZE, sort_asc, 3);

        EXPECT_EQ(ids.size(), 8) << "sort_asc: " << sort_asc;
        EXPECT_EQ(std::set<std::string>(ids.begin(), ids.end()).size(), 8) << "sort_asc: " << sort_asc;
    }

    database_close(db, FALSE);
}
