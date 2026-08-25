#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "tests/support/subprocess.h"

extern "C" {
#include "src/database/database.h"
#include "src/ctx.h"
#include "src/util.h"
#include "src/web/serve.h"
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

        std::ofstream file(dir / "files" / name, std::ios::binary);
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
        return sist2::test::run(command + sist2::test::quiet());
    }

    long long query(const std::string &sql) {
        return query_file(search_index, sql);
    }

    long long query_index(const std::string &sql) {
        return query_file(index, sql);
    }

    static long long query_file(const fs::path &file, const std::string &sql) {
        sqlite3 *db;
        if (sqlite3_open(file.string().c_str(), &db) != SQLITE_OK) {
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
        if (sqlite3_open(search_index.string().c_str(), &db) != SQLITE_OK) {
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
        if (sqlite3_open(search_index.string().c_str(), &db) != SQLITE_OK) {
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

    /** What `sist2 web` sets up so a highlight can read the document text back */
    database_t *load_index_database() {
        database_t *db = database_create(index.string().c_str(), INDEX_DATABASE);
        database_open(db);

        WebCtx.index_count = 1;
        WebCtx.indices[0].db = db;
        WebCtx.indices[0].desc.id = (int) query_index("SELECT id FROM descriptor");

        return db;
    }

    void unload_index_database(database_t *db) {
        WebCtx.index_count = 0;
        database_close(db, FALSE);
    }

    bool register_model(int model_id, int size) {
        return exec("INSERT INTO model (id, size) VALUES (" + std::to_string(model_id) + ", "
                    + std::to_string(size) + ")");
    }

    /** end < 0 writes NULL: the embedding covers the document from start to the end of its text */
    bool add_embedding(long long doc_id, int model_id, long long start, long long end,
                       const std::vector<float> &values) {
        return exec("INSERT INTO embedding (id, model_id, start, end, embedding) VALUES ("
                    + std::to_string(doc_id) + ", " + std::to_string(model_id) + ", "
                    + std::to_string(start) + ", " + (end < 0 ? "NULL" : std::to_string(end))
                    + ", " + blob_literal(values) + ")");
    }

    struct Hit {
        std::string id;
        std::string sort_value;
        std::string sort_tiebreaker;
        std::string name_highlight;
        std::string content_highlight;
        bool has_content_highlight = false;
        long long chunk_start = -1;
        long long chunk_end = -1;
        std::vector<int> hit_pages;
    };

    /** One page of results, with the cursor each hit would be paged after */
    std::vector<Hit> search(database_t *db, fts_sort_t sort, int sort_asc, int page_size,
                            char **after = nullptr, int model = 0,
                            const std::vector<float> &embedding = {},
                            const char *query = nullptr, int highlight = FALSE,
                            int context_size = 0, int fuzzy = FALSE) {
        cJSON *result = database_fts_search(db, query, nullptr, 0, 0, 0, 0, page_size, nullptr,
                                            nullptr, nullptr, sort_asc, sort, 0, after, FALSE,
                                            highlight, context_size, model,
                                            embedding.empty() ? nullptr : embedding.data(),
                                            (int) embedding.size(), fuzzy);

        std::vector<Hit> hits;
        if (result == nullptr) {
            return hits;
        }

        const cJSON *rows = cJSON_GetObjectItem(cJSON_GetObjectItem(result, "hits"), "hits");
        const cJSON *row;
        cJSON_ArrayForEach(row, rows) {
            const cJSON *sort_info = cJSON_GetObjectItem(row, "sort");

            Hit hit = {
                cJSON_GetObjectItem(row, "_id")->valuestring,
                cJSON_GetArrayItem(sort_info, 0)->valuestring,
                cJSON_GetArrayItem(sort_info, 1)->valuestring
            };

            const cJSON *highlighted = cJSON_GetObjectItem(row, "highlight");
            if (highlighted != nullptr) {
                const cJSON *name = cJSON_GetObjectItem(highlighted, "name");
                if (cJSON_IsString(name)) {
                    hit.name_highlight = name->valuestring;
                }

                const cJSON *content = cJSON_GetObjectItem(highlighted, "content");
                if (cJSON_IsString(content)) {
                    hit.content_highlight = content->valuestring;
                    hit.has_content_highlight = true;
                }
            }

            const cJSON *page;
            cJSON_ArrayForEach(page, cJSON_GetObjectItem(row, "hit_pages")) {
                hit.hit_pages.push_back((int) page->valuedouble);
            }

            const cJSON *chunk = cJSON_GetObjectItem(row, "chunk");
            if (chunk != nullptr) {
                hit.chunk_start = (long long) cJSON_GetObjectItem(chunk, "start")->valuedouble;
                hit.chunk_end = (long long) cJSON_GetObjectItem(chunk, "end")->valuedouble;
            }

            hits.push_back(hit);
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

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
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

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);

    // Every real run logs warnings; the mismatch is only formatted when they are on
    const int verbose = LogCtx.verbose;
    LogCtx.verbose = 1;

    std::vector<float> embedding(512, 0.5f);

    cJSON *result = database_fts_search(db, nullptr, nullptr, 0, 0, 0, 0, 10, nullptr, nullptr,
                                        nullptr, TRUE, FTS_SORT_SCORE, 0, nullptr, FALSE, FALSE,
                                        0, 1, embedding.data(), (int) embedding.size(), FALSE);

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
        ASSERT_TRUE(add_embedding(ids[0], 1, start, -1, {1, 0, 0, 0}));
    }

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
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
    ASSERT_TRUE(add_embedding(ids[3], 1, 0, -1, {0, 0, 0, 1}));
    ASSERT_TRUE(add_embedding(ids[3], 1, 1, -1, query));
    // Every other document has one chunk, none of them as good
    for (size_t i = 0; i < ids.size(); i++) {
        if (i != 3) {
            ASSERT_TRUE(add_embedding(ids[i], 1, 0, -1, {0.5f, 0.5f, 0, 0}));
        }
    }

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
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
    ASSERT_TRUE(add_embedding(ids[0], 1, 0, -1, query));

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
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

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);

    for (const int sort_asc: {1, 0}) {
        const std::vector<std::string> ids = page_through(db, FTS_SORT_SIZE, sort_asc, 3);

        EXPECT_EQ(ids.size(), 8) << "sort_asc: " << sort_asc;
        EXPECT_EQ(std::set<std::string>(ids.begin(), ids.end()).size(), 8) << "sort_asc: " << sort_asc;
    }

    database_close(db, FALSE);
}

/*
 * An embeddings search shows the chunk of the document that matched, not the head of it, and says
 * which byte range of the content that was.
 */
TEST_F(SqliteIndexTest, EmbeddingSearchExcerptsTheChunkThatMatched) {
    const std::string one = "SECTION ONE about apples and orchards. ";
    const std::string two = "SECTION TWO about submarines and sonar. ";
    const std::string three = "SECTION THREE about volcanoes and basalt. ";

    std::string text;
    for (int i = 0; i < 8; i++) text += one;
    const size_t two_at = text.size();
    for (int i = 0; i < 8; i++) text += two;
    const size_t three_at = text.size();
    for (int i = 0; i < 8; i++) text += three;

    write_file("sections.txt", text);

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const long long id = query("SELECT id FROM document_index WHERE name = 'sections'");
    ASSERT_GT(id, 0);

    ASSERT_TRUE(register_model(1, 3));
    ASSERT_TRUE(add_embedding(id, 1, 0, (long long) two_at, {1, 0, 0}));
    ASSERT_TRUE(add_embedding(id, 1, (long long) two_at, (long long) three_at, {0, 1, 0}));
    ASSERT_TRUE(add_embedding(id, 1, (long long) three_at, -1, {0, 0, 1}));

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const std::vector<std::vector<float>> queries = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const std::vector<std::string> expected = {"SECTION ONE", "SECTION TWO", "SECTION THREE"};
    const std::vector<long long> starts = {0, (long long) two_at, (long long) three_at};

    for (size_t i = 0; i < queries.size(); i++) {
        const std::vector<Hit> hits = search(db, FTS_SORT_EMBEDDING, FALSE, 10, nullptr, 1,
                                             queries[i], nullptr, TRUE, 6);

        ASSERT_FALSE(hits.empty());
        const Hit &hit = hits[0];

        EXPECT_EQ(hit.id, std::to_string(id)) << "query " << i;
        EXPECT_EQ(hit.chunk_start, starts[i]) << "query " << i;
        ASSERT_TRUE(hit.has_content_highlight) << "query " << i;
        EXPECT_EQ(hit.content_highlight.rfind(expected[i], 0), 0)
                            << "query " << i << ", excerpt: " << hit.content_highlight;
        // Only the chunk that matched, never the sections around it
        for (size_t j = 0; j < expected.size(); j++) {
            if (j != i) {
                EXPECT_EQ(hit.content_highlight.find(expected[j]), std::string::npos)
                                    << "query " << i << " leaked " << expected[j];
            }
        }
    }

    unload_index_database(index_db);
    database_close(db, FALSE);
}

/** A query alongside the embedding still marks its terms, inside the chunk that matched */
TEST_F(SqliteIndexTest, ChunkExcerptMarksTheQueryTerms) {
    std::string text;
    for (int i = 0; i < 8; i++) text += "alpha about apples and orchards. ";
    const size_t two_at = text.size();
    for (int i = 0; i < 8; i++) text += "alpha about submarines and sonar. ";

    write_file("sections.txt", text);

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const long long id = query("SELECT id FROM document_index WHERE name = 'sections'");

    ASSERT_TRUE(register_model(1, 2));
    ASSERT_TRUE(add_embedding(id, 1, 0, (long long) two_at, {1, 0}));
    ASSERT_TRUE(add_embedding(id, 1, (long long) two_at, -1, {0, 1}));

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const std::vector<Hit> hits = search(db, FTS_SORT_EMBEDDING, FALSE, 10, nullptr, 1, {0, 1},
                                         "submarines", TRUE, 8);

    ASSERT_FALSE(hits.empty());
    EXPECT_EQ(hits[0].chunk_start, (long long) two_at);
    EXPECT_NE(hits[0].content_highlight.find("<mark>submarines</mark>"), std::string::npos)
                        << "excerpt: " << hits[0].content_highlight;

    unload_index_database(index_db);
    database_close(db, FALSE);
}

/** Every hit keeps a name highlight, with or without a query to mark in it */
TEST_F(SqliteIndexTest, EmbeddingSearchWithoutAQueryStillHighlightsTheName) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const std::vector<long long> ids = document_ids();
    ASSERT_TRUE(register_model(1, 2));
    for (const long long id: ids) {
        ASSERT_TRUE(add_embedding(id, 1, 0, -1, {1, 0}));
    }

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const std::vector<Hit> hits = search(db, FTS_SORT_EMBEDDING, FALSE, 100, nullptr, 1, {1, 0},
                                         nullptr, TRUE, 10);

    ASSERT_EQ(hits.size(), 8);
    for (const Hit &hit: hits) {
        EXPECT_EQ(hit.name_highlight.rfind("file-", 0), 0) << "name: " << hit.name_highlight;
    }

    unload_index_database(index_db);
    database_close(db, FALSE);
}

/*
 * The offsets come from a user script, and the text can have been rewritten by a later scan since:
 * a range that does not fall inside the content falls back to all of it rather than to nothing.
 */
TEST_F(SqliteIndexTest, ChunkOutsideTheContentFallsBackToTheWholeText) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const long long id = document_ids()[0];

    ASSERT_TRUE(register_model(1, 2));
    ASSERT_TRUE(add_embedding(id, 1, 999999, 1000500, {1, 0}));

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const std::vector<Hit> hits = search(db, FTS_SORT_EMBEDDING, FALSE, 10, nullptr, 1, {1, 0},
                                         nullptr, TRUE, 30);

    ASSERT_FALSE(hits.empty());
    EXPECT_EQ(hits[0].chunk_start, 0);
    ASSERT_TRUE(hits[0].has_content_highlight);
    EXPECT_NE(hits[0].content_highlight.find("alpha zebra"), std::string::npos)
                        << "excerpt: " << hits[0].content_highlight;

    unload_index_database(index_db);
    database_close(db, FALSE);
}

/** Chunk boundaries are byte offsets, and a script is free to put one inside a character */
TEST_F(SqliteIndexTest, ChunkBoundaryInsideACharacterDoesNotCutIt) {
    // Eleven characters of three bytes each, so 50 and 140 both land on a continuation byte
    std::string text;
    for (int i = 0; i < 40; i++) {
        text += "\u65e5\u672c\u8a9e\u306e\u30c6\u30ad\u30b9\u30c8\u3067\u3059\u3002";
    }

    write_file("utf8.txt", text);

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    const long long id = query("SELECT id FROM document_index WHERE name = 'utf8'");

    ASSERT_TRUE(register_model(1, 2));
    ASSERT_TRUE(add_embedding(id, 1, 50, 140, {1, 0}));

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const std::vector<Hit> hits = search(db, FTS_SORT_EMBEDDING, FALSE, 10, nullptr, 1, {1, 0},
                                         nullptr, TRUE, 100);

    ASSERT_FALSE(hits.empty());
    // Forward to the start of the character each offset landed inside of
    EXPECT_EQ(hits[0].chunk_start, 51);
    EXPECT_EQ(hits[0].chunk_end, 141);

    // Every byte of the excerpt belongs to a whole UTF-8 sequence
    const std::string &excerpt = hits[0].content_highlight;
    ASSERT_FALSE(excerpt.empty());
    size_t i = 0;
    while (i < excerpt.size()) {
        const unsigned char c = excerpt[i];
        const size_t width = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        ASSERT_LE(i + width, excerpt.size()) << "truncated sequence at " << i;
        for (size_t k = 1; k < width; k++) {
            ASSERT_EQ(excerpt[i + k] & 0xC0, 0x80) << "bad continuation byte at " << i + k;
        }
        i += width;
    }

    unload_index_database(index_db);
    database_close(db, FALSE);
}

TEST_F(SqliteIndexTest, VocabularyIsBuiltFromTheIndexedWords) {
    write_file("hashes.txt", "encyclopedia deadbeef12345678");

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(query("SELECT count(*) FROM vocab_term WHERE term = 'encyclopedia'"), 1);
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term WHERE term = 'document'"), 1);

    // A long word with digits in it is an identifier, not something anybody misspells
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term WHERE term = 'deadbeef12345678'"), 0);

    // Every word of the vocabulary is a word of the search index, and knows its spellfix1 row
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term t"
                    " WHERE NOT EXISTS (SELECT 1 FROM search_vocab v WHERE v.term = t.term)"), 0);
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term t"
                    " WHERE NOT EXISTS (SELECT 1 FROM vocab_vocab s WHERE s.id = t.id)"), 0);
}

TEST_F(SqliteIndexTest, SkipSpellfixLeavesNoVocabulary) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index("--skip-spellfix"), 0);

    // -1: the table was never created
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term"), -1);
    EXPECT_EQ(matches("alpha"), 8);
}

/** A vocabulary that is left behind would be corrected against without ever being updated */
TEST_F(SqliteIndexTest, SkipSpellfixDropsAnExistingVocabulary) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    ASSERT_GT(query("SELECT count(*) FROM vocab_term"), 0);

    write_file("late.txt", "encyclopedia");

    ASSERT_EQ(scan("--incremental"), 0);
    ASSERT_EQ(sqlite_index("--skip-spellfix"), 0);

    EXPECT_EQ(query("SELECT count(*) FROM vocab_term"), -1);
    EXPECT_EQ(query("SELECT count(*) FROM vocab_vocab"), -1);

    // An index that is already up to date takes the same path
    ASSERT_EQ(sqlite_index("--skip-spellfix"), 0);
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term"), -1);
}

/** A number is looked up rather than misspelled: correcting one to another only costs results */
TEST_F(SqliteIndexTest, NumbersAreLeftOutOfTheVocabulary) {
    write_file("years.txt", "2024 1999 report");

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(query("SELECT count(*) FROM vocab_term WHERE term = 'report'"), 1);
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term WHERE term GLOB '[0-9]*' AND NOT term GLOB '*[^0-9]*'"), 0);
}

TEST_F(SqliteIndexTest, FuzzySearchFindsAMisspelledWord) {
    write_file("word.txt", "encyclopedia");

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    EXPECT_TRUE(search(db, FTS_SORT_SCORE, TRUE, 10, nullptr, 0, {}, "encyclopdia").empty());

    const std::vector<Hit> hits = search(db, FTS_SORT_SCORE, TRUE, 10, nullptr, 0, {},
                                         "encyclopdia", TRUE, 20, TRUE);

    ASSERT_EQ(hits.size(), 1);
    // The word that matched is what the excerpt marks, not the one that was typed
    EXPECT_NE(hits[0].content_highlight.find("<mark>encyclopedia</mark>"), std::string::npos)
                        << "excerpt: " << hits[0].content_highlight;

    unload_index_database(index_db);
    database_close(db, FALSE);
}

/** An index built with --skip-spellfix answers a fuzzy search the way a plain one does */
TEST_F(SqliteIndexTest, FuzzySearchWithoutAVocabularyMatchesWhatWasTyped) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index("--skip-spellfix"), 0);

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);

    EXPECT_EQ(search(db, FTS_SORT_SCORE, TRUE, 10, nullptr, 0, {}, "alpha", FALSE, 0, TRUE).size(), 8u);
    EXPECT_TRUE(search(db, FTS_SORT_SCORE, TRUE, 10, nullptr, 0, {}, "alpna", FALSE, 0, TRUE).empty());

    database_close(db, FALSE);
}

/**
 * A term is only expanded where fts5 takes an expression. Everywhere else the group it would be
 * replaced with is a syntax error, and a query that fails comes back as an empty result set: the
 * search silently finds nothing with Fuzzy on and everything with it off.
 */
TEST_F(SqliteIndexTest, FuzzySearchLeavesTermOnlyPositionsAlone) {
    // Near-spellings of the words the queries below use, so there is something to expand them with
    write_file("near.txt", "alpna zebar nome pith filt");

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);

    for (const char *query: {"\"alpha zebra\"", "zebr*", "alpha OR zebra", "name : file", "name:file",
                             "{name path} : file", "^alpha", "NEAR(alpha zebra, 5)",
                             "alpha + zebra"}) {
        const size_t plain = search(db, FTS_SORT_SCORE, TRUE, 20, nullptr, 0, {}, query).size();
        const size_t fuzzy = search(db, FTS_SORT_SCORE, TRUE, 20, nullptr, 0, {}, query, FALSE, 0, TRUE).size();

        EXPECT_GT(plain, 0u) << "query: " << query;
        // Fewer hits than the query that was typed means fts5 rejected the expanded one
        EXPECT_GE(fuzzy, plain) << "query: " << query;
    }

    // Expanding a phrase or a prefix term into alternatives would change what it means
    EXPECT_EQ(search(db, FTS_SORT_SCORE, TRUE, 20, nullptr, 0, {}, "\"alpha zebra\"", FALSE, 0, TRUE).size(), 8u);
    EXPECT_EQ(search(db, FTS_SORT_SCORE, TRUE, 20, nullptr, 0, {}, "zebr*", FALSE, 0, TRUE).size(), 8u);

    database_close(db, FALSE);
}

/**
 * fts5 concatenates adjacent phrases with an implicit AND, but two adjacent expressions have no
 * production of their own: the group a term is expanded into is a syntax error next to anything
 * the query did not spell an operator between, and a query that fails comes back empty.
 */
TEST_F(SqliteIndexTest, FuzzySearchCorrectsEveryWordOfAQuery) {
    write_file("near.txt", "alpna zebar documnet nome pith filt");

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);

    // No document has both words as they were typed, and correcting the second one finds them all
    EXPECT_TRUE(search(db, FTS_SORT_SCORE, TRUE, 20, nullptr, 0, {}, "alpha zebar").empty());
    EXPECT_EQ(search(db, FTS_SORT_SCORE, TRUE, 20, nullptr, 0, {}, "alpha zebar", FALSE, 0, TRUE).size(), 9u);

    for (const char *query: {"alpha zebra", "alpha zebra document", "alpha OR zebra",
                             "alpha AND zebra", "alpha NOT nothinghere", "name : file alpha",
                             "^alpha zebra", "alpha \"zebra document\"",
                             "alpha NEAR(zebra document, 5)", "alpha document + number",
                             "(alpha zebra) AND document"}) {
        const size_t plain = search(db, FTS_SORT_SCORE, TRUE, 20, nullptr, 0, {}, query).size();
        const size_t fuzzy = search(db, FTS_SORT_SCORE, TRUE, 20, nullptr, 0, {}, query, FALSE, 0, TRUE).size();

        EXPECT_GT(plain, 0u) << "query: " << query;
        // Fewer hits than the query that was typed means fts5 rejected the expanded one
        EXPECT_GE(fuzzy, plain) << "query: " << query;
    }

    database_close(db, FALSE);
}

TEST_F(SqliteIndexTest, IncrementalUpdateKeepsTheVocabularyInSync) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    ASSERT_EQ(query("SELECT count(*) FROM vocab_term WHERE term = 'zebra'"), 1);

    for (int i = 0; i < 8; i++) {
        write_file("file-" + std::to_string(i) + ".txt", "alpha encyclopedia number " + std::to_string(i));
    }

    ASSERT_EQ(scan("--incremental"), 0);
    ASSERT_EQ(sqlite_index(), 0);

    EXPECT_EQ(query("SELECT count(*) FROM vocab_term WHERE term = 'encyclopedia'"), 1);
    // Nothing has that word any more, so correcting a spelling to it would only cost results
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term WHERE term = 'zebra'"), 0);
    EXPECT_EQ(query("SELECT count(*) FROM vocab_vocab WHERE word = 'zebra'"), 0);

    // The vocabulary an incremental run keeps is the one a rebuild would have written
    const long long incremental = query("SELECT count(*) FROM vocab_term");
    ASSERT_EQ(sqlite_index("--rebuild"), 0);
    EXPECT_EQ(query("SELECT count(*) FROM vocab_term"), incremental);
}

/**
 * The search index stores no text, so a document it hands back carries the text read from the
 * index database it came from.
 */
TEST_F(SqliteIndexTest, GetDocumentReadsTheTextBack) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const std::vector<long long> ids = document_ids();
    ASSERT_FALSE(ids.empty());

    cJSON *json = database_fts_get_document(db, ids[0]);
    ASSERT_NE(json, nullptr);

    const cJSON *content = cJSON_GetObjectItem(json, "content");
    ASSERT_TRUE(cJSON_IsString(content));
    EXPECT_NE(strstr(content->valuestring, "alpha zebra document"), nullptr) << content->valuestring;

    cJSON_Delete(json);
    unload_index_database(index_db);
    database_close(db, FALSE);
}

/**
 * The page the excerpt of a result was taken from, so that a search result can be opened at the
 * page it matched on.
 */
TEST_F(SqliteIndexTest, SearchResultsCarryThePageTheyMatchedOn) {
    fs::copy_file(fs::path(SIST2_TEST_FILES_DIR) / "ebook" / "General_-_Candle_Making.pdf",
                  dir / "files" / "candles.pdf");

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const std::vector<Hit> hits = search(db, FTS_SORT_SCORE, FALSE, 10, nullptr, 0, {},
                                         "mould", TRUE, 30);

    ASSERT_EQ(hits.size(), 1);
    ASSERT_TRUE(hits[0].has_content_highlight);
    ASSERT_EQ(hits[0].hit_pages.size(), 1);
    // The word is first read on the second page of the leaflet
    EXPECT_EQ(hits[0].hit_pages[0], 2) << hits[0].content_highlight;

    unload_index_database(index_db);
    database_close(db, FALSE);
}

/** A document that is not paginated has no page to point at */
TEST_F(SqliteIndexTest, SearchResultsOfAPlainTextFileCarryNoPage) {
    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const std::vector<Hit> hits = search(db, FTS_SORT_SCORE, FALSE, 10, nullptr, 0, {},
                                         "zebra", TRUE, 30);

    ASSERT_FALSE(hits.empty());
    EXPECT_TRUE(hits[0].hit_pages.empty());

    unload_index_database(index_db);
    database_close(db, FALSE);
}

/**
 * The page each fragment of an Elasticsearch hit was taken from. Elasticsearch returns the passage
 * that matched but not where it was, so `sist2 web` fills it in from the index the document is in.
 */
TEST_F(SqliteIndexTest, ElasticsearchHitsCarryThePageOfEachFragment) {
    fs::copy_file(fs::path(SIST2_TEST_FILES_DIR) / "ebook" / "General_-_Candle_Making.pdf",
                  dir / "files" / "candles.pdf");

    ASSERT_EQ(scan(), 0);
    ASSERT_EQ(sqlite_index(), 0);

    database_t *db = database_create(search_index.string().c_str(), FTS_DATABASE);
    database_open(db);
    database_t *index_db = load_index_database();

    const long long sid = query("SELECT id FROM document_index WHERE name = \'candles\'");
    ASSERT_NE(sid, -1);

    cJSON *document = database_fts_get_document(db, sid);
    ASSERT_NE(document, nullptr);
    const char *page_breaks = cJSON_GetObjectItem(document, "page_breaks")->valuestring;

    char sid_str[SIST_SID_LEN];
    format_sid(sid_str, (int) (sid >> 32), (int) (sid & 0xFFFFFFFF));

    cJSON *hit = cJSON_CreateObject();
    cJSON_AddStringToObject(hit, "_id", sid_str);
    cJSON_AddStringToObject(cJSON_AddObjectToObject(hit, "_source"), "page_breaks", page_breaks);

    cJSON *fragments = cJSON_AddArrayToObject(cJSON_AddObjectToObject(hit, "highlight"), "content");
    cJSON_AddItemToArray(fragments, cJSON_CreateString("Prepare the <mark>Mould</mark> 1. Make sure"));
    cJSON_AddItemToArray(fragments, cJSON_CreateString("To Remove the <mark>Mould</mark>"));
    cJSON_AddItemToArray(fragments, cJSON_CreateString("nowhere in the <mark>document</mark>"));

    cJSON *response = cJSON_CreateObject();
    cJSON *hits = cJSON_AddArrayToObject(cJSON_AddObjectToObject(response, "hits"), "hits");
    cJSON_AddItemToArray(hits, hit);

    char *body = cJSON_PrintUnformatted(response);
    char *annotated = es_add_hit_pages(body, strlen(body));
    ASSERT_NE(annotated, nullptr);

    cJSON *parsed = cJSON_Parse(annotated);
    const cJSON *annotated_hit =
            cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(parsed, "hits"), "hits"), 0);
    const cJSON *pages = cJSON_GetObjectItem(annotated_hit, "hit_pages");

    // Nothing downstream reads the offsets themselves
    EXPECT_EQ(cJSON_GetObjectItem(cJSON_GetObjectItem(annotated_hit, "_source"), "page_breaks"),
              nullptr);

    ASSERT_EQ(cJSON_GetArraySize(pages), 3);
    EXPECT_EQ(cJSON_GetArrayItem(pages, 0)->valuedouble, 6);
    EXPECT_EQ(cJSON_GetArrayItem(pages, 1)->valuedouble, 9);
    // A fragment that is not part of the text has no page
    EXPECT_EQ(cJSON_GetArrayItem(pages, 2)->valuedouble, 0);

    cJSON_Delete(parsed);
    cJSON_Delete(response);
    cJSON_Delete(document);
    free(annotated);
    free(body);
    unload_index_database(index_db);
    database_close(db, FALSE);
}

/** A response with no paginated document is left alone */
TEST_F(SqliteIndexTest, ElasticsearchHitsOfAPlainTextFileAreLeftAlone) {
    const char *body = R"({"hits":{"hits":[{"_id":"0000000a.0000000b","_source":{"name":"x"}}]}})";

    ASSERT_EQ(es_add_hit_pages(body, strlen(body)), nullptr);
}

/**
 * A request that asks for the text of a document is not a search: its one fragment is the whole
 * document, which would be read back and scanned for nothing.
 */
TEST_F(SqliteIndexTest, ElasticsearchHitsThatCarryTheirTextAreLeftAlone) {
    const char *body = R"({"hits":{"hits":[{"_id":"0000000a.0000000b","_source":)"
                       R"({"content":"page one text","page_breaks":"0,14"},)"
                       R"("highlight":{"content":["page <mark>one</mark> text"]}}]}})";

    ASSERT_EQ(es_add_hit_pages(body, strlen(body)), nullptr);
}
