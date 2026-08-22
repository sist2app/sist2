#include "tests/support/scan_fixture.h"

extern "C" {
#include "libscan/wpd/libwpd_c_api.h"
}

class WpdTest : public ScanTest {
protected:
    scan_wpd_ctx_t ctx = make_wpd_ctx();
};

TEST_F(WpdTest, Wpd51_1) {
    load("wpd/test51_1.wpd");

    parse_wpd(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "Hello, WordPerfect This is a test This is the next page This is another page");
}

/*
 * The document summary of a WordPerfect file: libwpd hands it over field by field, and the three
 * fields sist2 has a place for are kept. No corpus file carries a filled-in summary — of the 21
 * real-world documents attached to issue #159, one has a creation date and the rest have nothing
 * — so the mapping is driven directly.
 */
TEST_F(WpdTest, SummaryFieldsAreStored) {
    wpd_set_meta(&doc, "meta:initial-creator", "Ada Lovelace");
    wpd_set_meta(&doc, "libwpd:descriptive-name", "Notes on the Analytical Engine");
    wpd_set_meta(&doc, "libwpd:editor", "Charles Babbage");

    ASSERT_NE(meta(MetaAuthor), nullptr);
    EXPECT_STREQ(meta(MetaAuthor)->str_val, "Ada Lovelace");
    ASSERT_NE(meta(MetaTitle), nullptr);
    EXPECT_STREQ(meta(MetaTitle)->str_val, "Notes on the Analytical Engine");
    ASSERT_NE(meta(MetaModifiedBy), nullptr);
    EXPECT_STREQ(meta(MetaModifiedBy)->str_val, "Charles Babbage");
}

/** dc:creator is what a WordPerfect 6+ file carries instead of meta:initial-creator */
TEST_F(WpdTest, AuthorComesFromEitherKey) {
    wpd_set_meta(&doc, "dc:creator", "Grace Hopper");

    ASSERT_NE(meta(MetaAuthor), nullptr);
    EXPECT_STREQ(meta(MetaAuthor)->str_val, "Grace Hopper");
}

/** A summary field sist2 has no place for, and an empty one, are dropped rather than stored blank */
TEST_F(WpdTest, UnmappedAndEmptyFieldsAreIgnored) {
    wpd_set_meta(&doc, "libwpd:telephone-number", "555-0100");
    wpd_set_meta(&doc, "meta:initial-creator", "");

    EXPECT_EQ(doc.meta_head, nullptr);
}

/**
 * wpd.c creates its text buffer with text_buffer_create(-1), so ctx->content_size has no effect
 * and the whole document is indexed. Change this test when that is fixed.
 */
TEST_F(WpdTest, ContentSizeIsIgnored) {
    ctx = make_wpd_ctx(20);
    load("wpd/test51_1.wpd");

    parse_wpd(&ctx, &f, &doc);

    ASSERT_EQ(content_len(), 76);
}
