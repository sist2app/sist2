#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <filesystem>
#include <string>
#include <vector>

#include "tests/support/temp_path.h"

extern "C" {
#include "src/database/database.h"
#include "src/ctx.h"
}

/**
 * An index database in a scratch file. The ipc context is normally shared memory set up by the
 * thread pool; the tests only need its mutexes to be valid.
 */
class DatabaseTest : public ::testing::Test {
protected:
    std::string db_path = temp_path("database.sist2");
    std::string scan_root = temp_path("scan-root");
    database_t *db = nullptr;

    void SetUp() override {
        std::filesystem::remove(db_path);


        strcpy(ScanCtx.index.desc.root, scan_root.c_str());
        ScanCtx.index.desc.root_len = (int) scan_root.size() + 1;

        db = database_create(db_path.c_str(), INDEX_DATABASE);

        database_initialize(db);
        database_open(db);

        index_descriptor_t desc = {};
        desc.id = 1;
        desc.timestamp = 1600000000;
        desc.version_major = VersionMajor;
        strcpy(desc.version, Version);
        strcpy(desc.name, "test index");
        strcpy(desc.root, scan_root.c_str());
        desc.root_len = ScanCtx.index.desc.root_len;

        database_write_index_descriptor(db, &desc);

        // Documents are written against the current index version
        database_increment_version(db);
    }

    void TearDown() override {
        database_close(db, FALSE);
        std::filesystem::remove(db_path);
    }

    int temp_store() const {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db->db, "PRAGMA temp_store", -1, &stmt, nullptr) != SQLITE_OK) {
            return -1;
        }

        const int result = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
        sqlite3_finalize(stmt);
        return result;
    }

    /** A model a user script would have registered, and one chunk of a document it embedded */
    void write_embedding(int doc_id, int start, int end, float value, int model_id = 1,
                         const char *model_path = "idx_384.test") {
        char sql[512];
        snprintf(sql, sizeof(sql),
                 "INSERT OR IGNORE INTO model (id, name, url, path, size, type)"
                 " VALUES (%d, 'test%d', NULL, '%s', 384, 'flat');",
                 model_id, model_id, model_path);
        ASSERT_EQ(sqlite3_exec(db->db, sql, nullptr, nullptr, nullptr), SQLITE_OK);

        std::vector<float> embedding(384, value);

        sqlite3_stmt *stmt;
        ASSERT_EQ(sqlite3_prepare_v2(db->db,
                                     "INSERT INTO embedding (id, model_id, start, end, embedding)"
                                     " VALUES (?,?,?,?,?)", -1, &stmt, nullptr), SQLITE_OK);
        sqlite3_bind_int(stmt, 1, doc_id);
        sqlite3_bind_int(stmt, 2, model_id);
        sqlite3_bind_int(stmt, 3, start);
        sqlite3_bind_int(stmt, 4, end);
        sqlite3_bind_blob(stmt, 5, embedding.data(), (int) (embedding.size() * sizeof(float)),
                          SQLITE_STATIC);

        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_finalize(stmt);
    }

    document_t make_document(const std::string &relative_path, unsigned long size = 1234) {
        document_t doc = {};

        snprintf(doc.filepath, sizeof(doc.filepath), "%s/%s", scan_root.c_str(), relative_path.c_str());
        doc.size = size;
        doc.mtime = 1600000000;
        doc.mime = 590195; // text/plain

        return doc;
    }
};

/** The default sets in-memory temporary storage for better performance. */
TEST_F(DatabaseTest, DefaultModeForcesTemporaryDataIntoMemory) {
    EXPECT_EQ(temp_store(), 2);
}

/** Low-memory mode deliberately does not force temporary storage into memory for an index database. */
TEST_F(DatabaseTest, LowMemoryModeDoesNotOverrideSqliteTemporaryStorage) {
    database_close(db, FALSE);
    database_open_with_options(db, TRUE);

    EXPECT_EQ(temp_store(), 0);
}

/** What SQLite would use for its temporary files, the way database.c looks it up */
static const char *temp_directory() {
    const char *directory = getenv("SQLITE_TMPDIR");
    if (directory == nullptr) {
        directory = getenv("TMPDIR");
    }
    if (directory != nullptr) {
        return directory;
    }

    return sist_temp_dir();
}

/**
 * A full disk is the one SQLite error a user can act on, but only if the message says which file
 * ran out of room: the index, the search index and the temporary files are often on three
 * different filesystems.
 */
TEST_F(DatabaseTest, AFullDiskErrorNamesTheFilesAndTheirFreeSpace) {
    ASSERT_DEATH(database_fatal_sqlite_error(db, "test.c", 1, SQLITE_FULL),
                 ::testing::AllOf(
                         ::testing::HasSubstr("index database"),
                         ::testing::HasSubstr("MiB free"),
                         ::testing::HasSubstr("temporary file folder (" + std::string(temp_directory()) + ")"),
                         ::testing::HasSubstr("(13) database or disk is full")));
}

/** Every other error is reported as it always was */
TEST_F(DatabaseTest, AnOrdinarySqliteErrorIsReportedWithoutDiskDetails) {
    ASSERT_DEATH(database_fatal_sqlite_error(db, "test.c", 1, SQLITE_ERROR),
                 ::testing::AllOf(
                         ::testing::HasSubstr("Sqlite error @ test.c:1"),
                         ::testing::Not(::testing::HasSubstr("MiB free"))));
}

TEST_F(DatabaseTest, IndexDescriptorRoundTrip) {
    index_descriptor_t desc = {};
    desc.id = 42;
    desc.timestamp = 1600000000;
    desc.version_major = 3;
    desc.version_minor = 5;
    desc.version_patch = 0;
    strcpy(desc.version, "3.5.0");
    strcpy(desc.name, "test index");
    strcpy(desc.root, scan_root.c_str());
    desc.root_len = (int) scan_root.size() + 1;

    database_write_index_descriptor(db, &desc);

    index_descriptor_t *read = database_read_index_descriptor(db);

    ASSERT_NE(read, nullptr);
    ASSERT_EQ(read->id, 42);
    ASSERT_STREQ(read->name, "test index");
    ASSERT_STREQ(read->version, "3.5.0");
    ASSERT_STREQ(read->root, scan_root.c_str());

    free(read);
}

TEST_F(DatabaseTest, WriteDocumentReturnsIncreasingIds) {
    document_t doc1 = make_document("a.txt");
    document_t doc2 = make_document("b.txt");

    const int id1 = database_write_document(db, &doc1, R"({"name": "a"})");
    const int id2 = database_write_document(db, &doc2, R"({"name": "b"})");

    ASSERT_GT(id1, 0);
    ASSERT_GT(id2, id1);
}

TEST_F(DatabaseTest, DocumentIteratorReturnsWrittenDocuments) {
    document_t doc = make_document("folder/file.txt");
    database_write_document(db, &doc, R"({"name": "file.txt", "extension": "txt"})");

    database_iterator_t *iter = database_create_document_iterator(db, 0);

    cJSON *row = database_document_iter(iter);

    ASSERT_NE(row, nullptr);
    ASSERT_STREQ(cJSON_GetObjectItem(row, "name")->valuestring, "file.txt");

    cJSON_Delete(row);

    ASSERT_EQ(database_document_iter(iter), nullptr);

    free(iter);
}

TEST_F(DatabaseTest, ThumbnailRoundTrip) {
    document_t doc = make_document("image.jpg");
    const int doc_id = database_write_document(db, &doc, R"({"name": "image.jpg"})");

    const char data[] = "not-really-a-jpeg";
    database_write_thumbnail(db, doc_id, 0, (void *) data, sizeof(data));

    size_t read_len = 0;
    void *read = database_read_thumbnail(db, doc_id, 0, &read_len);

    ASSERT_NE(read, nullptr);
    ASSERT_EQ(read_len, sizeof(data));
    ASSERT_EQ(memcmp(read, data, sizeof(data)), 0);

    free(read);
}

TEST_F(DatabaseTest, MissingThumbnailReadsAsNull) {
    size_t read_len = 0;

    ASSERT_EQ(database_read_thumbnail(db, 999, 0, &read_len), nullptr);
    ASSERT_EQ(read_len, 0);
}

/** Documents not marked during an incremental scan end up in the delete list */
TEST_F(DatabaseTest, IncrementalScanDeleteList) {
    document_t kept = make_document("kept.txt");
    document_t removed = make_document("removed.txt");

    database_write_document(db, &kept, R"({"name": "kept.txt"})");
    database_write_document(db, &removed, R"({"name": "removed.txt"})");

    database_incremental_scan_begin(db);
    ASSERT_TRUE(database_mark_document(db, "kept.txt", kept.mtime));
    database_incremental_scan_end(db);

    database_iterator_t *iter = database_create_delete_list_iterator(db);

    int deleted_count = 0;
    while (database_delete_list_iter(iter) != 0) {
        deleted_count += 1;
    }

    ASSERT_EQ(deleted_count, 1);

    free(iter);
}

/** Documents extracted from an archive point back to the file they came from */
TEST_F(DatabaseTest, ParentIdOfArchiveMember) {
    document_t archive = make_document("files.zip");
    const int archive_id = database_write_document(db, &archive, R"({"name": "files.zip"})");

    document_t member = make_document("files.zip#/inside.txt");
    strcpy(member.parent, archive.filepath);
    const int member_id = database_write_document(db, &member, R"({"name": "inside.txt"})");

    ASSERT_EQ(database_get_parent_id(db, member_id), archive_id);
    ASSERT_EQ(database_get_parent_id(db, archive_id), DATABASE_NO_PARENT);
}

TEST_F(DatabaseTest, ParentIdOfMissingDocument) {
    ASSERT_EQ(database_get_parent_id(db, 999), DATABASE_NO_PARENT);
}

/**
 * Elasticsearch cannot read the text back at search time the way the SQLite backend does, so every
 * chunk carries the passage it was generated from, ready to be quoted as the excerpt of a hit.
 */
TEST_F(DatabaseTest, DocumentIteratorCarriesEveryChunkWithItsText) {
    document_t doc = make_document("book.txt");
    const int doc_id = database_write_document(db, &doc, R"({"name": "book", "content": "one two three four"})");

    write_embedding(doc_id, 0, 7, 0.5f);
    write_embedding(doc_id, 8, 18, 0.25f);

    database_iterator_t *iter = database_create_document_iterator(db, 0);
    cJSON *row = database_document_iter(iter);

    ASSERT_NE(row, nullptr);

    cJSON *chunks = cJSON_GetObjectItem(row, "emb_chunks");
    ASSERT_TRUE(cJSON_IsArray(chunks));
    ASSERT_EQ(cJSON_GetArraySize(chunks), 2);

    const cJSON *first = cJSON_GetArrayItem(chunks, 0);
    EXPECT_STREQ(cJSON_GetObjectItem(first, "text")->valuestring, "one two");
    EXPECT_EQ(cJSON_GetObjectItem(first, "start")->valueint, 0);
    EXPECT_EQ(cJSON_GetObjectItem(first, "end")->valueint, 7);

    const cJSON *second = cJSON_GetArrayItem(chunks, 1);
    EXPECT_STREQ(cJSON_GetObjectItem(second, "text")->valuestring, "three four");

    // The vector is keyed on the model path, the same way the document-level one is
    const cJSON *embedding = cJSON_GetObjectItem(cJSON_GetObjectItem(first, "emb"), "idx_384.test");
    ASSERT_TRUE(cJSON_IsArray(embedding));
    EXPECT_EQ(cJSON_GetArraySize(embedding), 384);

    cJSON_Delete(row);

    ASSERT_EQ(database_document_iter(iter), nullptr);
    free(iter);
}

/** A chunk boundary that lands inside a UTF-8 sequence must not cut it in half */
TEST_F(DatabaseTest, ChunkTextIsCutOnCharacterBoundaries) {
    document_t doc = make_document("accents.txt");
    // "Café" is five bytes: the é starts at 3
    const int doc_id = database_write_document(db, &doc, R"({"name": "accents", "content": "Café au lait"})");

    write_embedding(doc_id, 4, 13, 0.5f);

    database_iterator_t *iter = database_create_document_iterator(db, 0);
    cJSON *row = database_document_iter(iter);

    ASSERT_NE(row, nullptr);

    const cJSON *chunks = cJSON_GetObjectItem(row, "emb_chunks");
    ASSERT_TRUE(cJSON_IsArray(chunks));

    const char *text = cJSON_GetObjectItem(cJSON_GetArrayItem(chunks, 0), "text")->valuestring;
    EXPECT_STREQ(text, " au lait");

    cJSON_Delete(row);

    ASSERT_EQ(database_document_iter(iter), nullptr);
    free(iter);
}

/** A document nothing embedded is pushed the way it always was */
TEST_F(DatabaseTest, DocumentIteratorLeavesDocumentsWithoutEmbeddingsAlone) {
    document_t doc = make_document("plain.txt");
    database_write_document(db, &doc, R"({"name": "plain", "content": "nothing to see"})");

    database_iterator_t *iter = database_create_document_iterator(db, 0);
    cJSON *row = database_document_iter(iter);

    ASSERT_NE(row, nullptr);
    EXPECT_EQ(cJSON_GetObjectItem(row, "emb_chunks"), nullptr);
    EXPECT_EQ(cJSON_GetObjectItem(row, "embedding"), nullptr);

    cJSON_Delete(row);

    ASSERT_EQ(database_document_iter(iter), nullptr);
    free(iter);
}

/** Chunks that start past the beginning of the text are still pushed, whole */
TEST_F(DatabaseTest, DocumentIteratorCarriesChunksWithoutADocumentVector) {
    document_t doc = make_document("offset.txt");
    const int doc_id = database_write_document(db, &doc, R"({"name": "offset", "content": "one two three"})");

    write_embedding(doc_id, 4, 13, 0.5f);

    database_iterator_t *iter = database_create_document_iterator(db, 0);
    cJSON *row = database_document_iter(iter);

    ASSERT_NE(row, nullptr);
    // emb.<path> holds the first chunk of the document, and this one has none
    EXPECT_EQ(cJSON_GetObjectItem(row, "emb"), nullptr);
    EXPECT_TRUE(cJSON_IsArray(cJSON_GetObjectItem(row, "emb_chunks")));
    EXPECT_EQ(cJSON_GetObjectItem(row, "embedding")->valueint, 1);

    cJSON_Delete(row);

    ASSERT_EQ(database_document_iter(iter), nullptr);
    free(iter);
}
