#include "tests/support/scan_fixture.h"

class WpdTest : public ScanTest {
protected:
    scan_wpd_ctx_t ctx = make_wpd_ctx();
};

TEST_F(WpdTest, Wpd51_1) {
    load("wpd/test51_1.wpd");

    parse_wpd(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "Hello, WordPerfect This is a test This is the next page This is another page");
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
