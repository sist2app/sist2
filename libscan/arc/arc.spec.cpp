#include "tests/support/scan_fixture.h"

/** Reads every entry to completion without parsing it */
static void drain_entry(parse_job_t *job) {
    char buf[1024];

    while (job->vfile.read(&job->vfile, buf, sizeof(buf)) > 0) {}
}

class ArcListTest : public ScanTest {
protected:
    scan_arc_ctx_t ctx = make_arc_ctx(ARC_MODE_LIST);
};

TEST_F(ArcListTest, Utf8FileNames) {
    load("arc/test1.zip");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_TRUE(strstr(content(), "arctest/ȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬȬ.txt") != nullptr);
}

TEST_F(ArcListTest, ListDoesNotRecurse) {
    load("arc/vid3.tar");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_NE(meta(MetaContent), nullptr);
    ASSERT_EQ(thumbnails_size(), 0);
}

class ArcRecurseTest : public ArcTest {
protected:
    scan_arc_ctx_t ctx = make_arc_ctx(ARC_MODE_RECURSE);
};

/** A zip bomb must not be expanded recursively until the process dies */
TEST_F(ArcRecurseTest, ZipBomb) {
    scan_arc_ctx_t noop_ctx = make_arc_ctx(ARC_MODE_RECURSE);
    noop_ctx.parse = drain_entry;

    load("arc/bomb.zip");

    parse_archive(&noop_ctx, &f, &doc, nullptr, nullptr);
}

TEST_F(ArcRecurseTest, SubDocumentPathsAndCount) {
    recurse_into([](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
    });

    load("arc/test1.zip");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_GT(sub_docs.size(), 0);
    for (size_t i = 0; i < sub_docs.size(); i++) {
        // Sub-document paths look like <archive path>#/<entry path>
        ASSERT_TRUE(strstr(sub_docs.at(i)->filepath, "#/") != nullptr) << sub_docs.at(i)->filepath;
    }
}

TEST_F(ArcRecurseTest, EncryptedZip) {
    ctx = make_arc_ctx(ARC_MODE_RECURSE, "sist2");
    scan_media_ctx_t media_ctx = make_media_ctx();

    recurse_into([&media_ctx](parse_job_t *job, document_t *sub_doc) {
        parse_media(&media_ctx, &job->vfile, sub_doc, "image/jpeg");
    });

    load("arc/encrypted.zip");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    ASSERT_NE(thumbnail_size(sub_docs.last()), 0);
}

/** Without the passphrase, the entries cannot be read and no sub-document is produced */
TEST_F(ArcRecurseTest, EncryptedZipWrongPassphrase) {
    ctx = make_arc_ctx(ARC_MODE_RECURSE, "not-the-passphrase");
    scan_media_ctx_t media_ctx = make_media_ctx();

    recurse_into([&media_ctx](parse_job_t *job, document_t *sub_doc) {
        parse_media(&media_ctx, &job->vfile, sub_doc, "image/jpeg");
    });

    load("arc/encrypted.zip");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(thumbnail_size(sub_docs.last()), 0);
}

/** Only multi-extension tar variants are worth opening when the mime type says "filtered file" */
TEST(ArcFilter, FilteredFileNames) {
    ASSERT_TRUE(should_parse_filtered_file("/tmp/archive.tar.gz"));
    ASSERT_TRUE(should_parse_filtered_file("/tmp/archive.tar.bz2"));
    ASSERT_TRUE(should_parse_filtered_file("/tmp/archive.tgz"));
    ASSERT_FALSE(should_parse_filtered_file("/tmp/archive.gz"));
    ASSERT_FALSE(should_parse_filtered_file("/tmp/archive.xz"));
}
