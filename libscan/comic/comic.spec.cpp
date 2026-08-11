#include "tests/support/scan_fixture.h"

class ComicTest : public ScanTest {
protected:
    scan_comic_ctx_t ctx = make_comic_ctx();
};

TEST_F(ComicTest, Cbz) {
    load("ebook/lost_treasure.cbz");

    parse_comic(&ctx, &f, &doc);

    ASSERT_NE(thumbnails_size(), 0);
}

TEST_F(ComicTest, Cbr) {
    load("ebook/laugh.cbr");

    parse_comic(&ctx, &f, &doc);

    ASSERT_NE(thumbnails_size(), 0);
}

TEST_F(ComicTest, Issue160) {
    ctx = make_comic_ctx(500, FALSE);
    load("ebook/comic-segfault-issue-160.cbr");

    parse_comic(&ctx, &f, &doc);

    ASSERT_EQ(thumbnails_size(), 0);
}

/** A thumbnail bigger than the source image is stored as-is instead of being scaled up */
TEST_F(ComicTest, CbrAsIs) {
    ctx = make_comic_ctx(5000);
    load("ebook/laugh.cbr");

    parse_comic(&ctx, &f, &doc);

    ASSERT_EQ(thumbnails_size(), 92451);
}

TEST_F(ComicTest, CbrFilters) {
    load("ebook/cannot_parse_filters.cbr");

    parse_comic(&ctx, &f, &doc);
}

TEST_F(ComicTest, ThumbnailDisabled) {
    ctx = make_comic_ctx(500, FALSE);
    load("ebook/lost_treasure.cbz");

    parse_comic(&ctx, &f, &doc);

    ASSERT_EQ(thumbnails_size(), 0);
}
