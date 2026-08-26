#include "tests/support/scan_fixture.h"

class OoxmlTest : public ScanTest {
protected:
    scan_ooxml_ctx_t ctx = make_ooxml_ctx();
};

TEST_F(OoxmlTest, Pptx1) {
    load("ooxml/Catalist Presentation.pptx");

    parse_ooxml(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaTitle)->str_val, "Slide 1");
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "thofeller");
    ASSERT_STREQ(meta(MetaModifiedBy)->str_val, "Hofeller");
    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(OoxmlTest, Docx1) {
    load("ooxml/How To Play A DVD On Windows 8.docx");

    parse_ooxml(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Thomas");
    ASSERT_STREQ(meta(MetaModifiedBy)->str_val, "Thomas");
    ASSERT_EQ(meta(MetaPages)->long_val, 2);
    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(OoxmlTest, Docx2) {
    ctx = make_ooxml_ctx(999999);
    load("ooxml/docx2.docx");

    parse_ooxml(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaAuthor)->str_val, "liz evans");
    ASSERT_EQ(meta(MetaPages)->long_val, 1);
    ASSERT_EQ(content_len(), 2794);
}

TEST_F(OoxmlTest, Docx2Thumbnail) {
    load("ooxml/embed_tn.docx");

    parse_ooxml(&ctx, &f, &doc);

    ASSERT_NEAR(content_len(), 500, 4);
    ASSERT_EQ(meta(MetaPages)->long_val, 2);
    ASSERT_NE(thumbnails_size(), 0);
    ASSERT_EQ(doc.thumbnail_count, 1);
}

TEST_F(OoxmlTest, Docx2ThumbnailDisabled) {
    ctx = make_ooxml_ctx(500, FALSE);
    load("ooxml/embed_tn.docx");

    parse_ooxml(&ctx, &f, &doc);

    ASSERT_EQ(thumbnails_size(), 0);
    ASSERT_EQ(doc.thumbnail_count, 0);
}

TEST_F(OoxmlTest, Xlsx1) {
    load("ooxml/xlsx1.xlsx");

    parse_ooxml(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Bureau of Economic Analysis");
    ASSERT_STREQ(meta(MetaModifiedBy)->str_val, "lz");
    ASSERT_NEAR(content_len(), 500, 4);
}

/** ooxml documents read through an archive vfile rather than the filesystem */
class OoxmlArcTest : public ArcTest {
protected:
    scan_ooxml_ctx_t ooxml_ctx = make_ooxml_ctx(999999);
    scan_arc_ctx_t ctx = make_arc_ctx(ARC_MODE_RECURSE);

    void SetUp() override {
        recurse_into([this](parse_job_t *job, document_t *sub_doc) {
            parse_ooxml(&ooxml_ctx, &job->vfile, sub_doc);
        });
    }
};

TEST_F(OoxmlArcTest, Docx2In7z) {
    load("ooxml/docx2.docx.7z");

    parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    ASSERT_EQ(sub_docs.size(), 1);
    ASSERT_STREQ(get_meta(sub_docs.last(), MetaAuthor)->str_val, "liz evans");
    ASSERT_EQ(get_meta(sub_docs.last(), MetaPages)->long_val, 1);
    ASSERT_EQ(strlen(get_meta(sub_docs.last(), MetaContent)->str_val), 2794);
}
