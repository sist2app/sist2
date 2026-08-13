#include <gtest/gtest.h>

#include <filesystem>
#include <string>

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

    document_t make_document(const std::string &relative_path, unsigned long size = 1234) {
        document_t doc = {};

        snprintf(doc.filepath, sizeof(doc.filepath), "%s/%s", scan_root.c_str(), relative_path.c_str());
        doc.size = size;
        doc.mtime = 1600000000;
        doc.mime = 590195; // text/plain

        return doc;
    }
};

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
