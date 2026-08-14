#include "tests/support/scan_fixture.h"
#include "tests/support/temp_path.h"

#include <fstream>

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

/** A compressed file that is not an archive yields its single member */
TEST_F(ArcRecurseTest, GzipStream) {
    scan_text_ctx_t text_ctx = make_text_ctx();

    recurse_into([&text_ctx](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
        parse_text(&text_ctx, &job->vfile, sub_doc);
    });

    load("arc/text.txt.gz");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    // The gzip header carries the name of the file it was made from
    ASSERT_TRUE(strstr(sub_docs.last()->filepath, "#/text.txt") != nullptr) << sub_docs.last()->filepath;
    ASSERT_STREQ(get_meta(sub_docs.last(), MetaContent)->str_val, "the gzip member content marker");
}

/** Without a name in the header, the member is named after the file, minus its extension */
TEST_F(ArcRecurseTest, GzipStreamWithoutStoredName) {
    scan_text_ctx_t text_ctx = make_text_ctx();

    recurse_into([&text_ctx](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
        parse_text(&text_ctx, &job->vfile, sub_doc);
    });

    load("arc/nameless.gz");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    ASSERT_TRUE(strstr(sub_docs.last()->filepath, "#/nameless") != nullptr) << sub_docs.last()->filepath;
    ASSERT_STREQ(get_meta(sub_docs.last(), MetaContent)->str_val, "the nameless gzip content marker");
}

/** One member over the decompressed size ratio must not cut the rest of the archive short */
TEST_F(ArcRecurseTest, SuspiciousMemberDoesNotStopTheArchive) {
    scan_text_ctx_t text_ctx = make_text_ctx();

    recurse_into([&text_ctx](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
        parse_text(&text_ctx, &job->vfile, sub_doc);
    });

    load("arc/bomb-member.zip");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    ASSERT_TRUE(strstr(sub_docs.last()->filepath, "#/after.txt") != nullptr) << sub_docs.last()->filepath;
}

/**
 * A file named like an archive but holding something else is read by the raw format, which names
 * its single member after the file. That member must not keep the extension that sent it here, or
 * every one of them is opened as an archive again, forever.
 */
TEST_F(ArcRecurseTest, MislabeledArchiveMemberHasNoExtension) {
    static const char text[] = "this is not an archive";

    scan_text_ctx_t text_ctx = make_text_ctx();
    int member_ext = -1;

    recurse_into([&](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
        member_ext = job->ext;
        parse_text(&text_ctx, &job->vfile, sub_doc);
    });

    const std::string path = temp_path("mislabeled.zip");
    const std::string name = path.substr(path.rfind('/') + 1);
    std::ofstream(path) << text;

    load_path(path);

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    // The raw format has no name for its member, so it is named after the file
    ASSERT_EQ(std::string(sub_docs.last()->filepath), path + "#/" + name);
    // Points at the terminator, so the mime comes from the content
    ASSERT_EQ(member_ext, (int) strlen(sub_docs.last()->filepath));
    ASSERT_STREQ(get_meta(sub_docs.last(), MetaContent)->str_val, text);

    unlink(path.c_str());
}

/** A member with a compression extension stripped keeps whatever extension is underneath */
TEST_F(ArcRecurseTest, CompressedStreamMemberKeepsItsExtension) {
    scan_text_ctx_t text_ctx = make_text_ctx();
    int member_ext = -1;

    recurse_into([&](parse_job_t *job, document_t *sub_doc) {
        strcpy(sub_doc->filepath, job->filepath);
        member_ext = job->ext;
        parse_text(&text_ctx, &job->vfile, sub_doc);
    });

    load("arc/text.txt.gz");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    ASSERT_STREQ(sub_docs.last()->filepath + member_ext, "txt");
}
