#include "tests/support/scan_fixture.h"

class FontTest : public ScanTest {
protected:
    scan_font_ctx_t ctx = make_font_ctx();
};

TEST_F(FontTest, TrueType) {
    load("font/truetype1.ttf");

    parse_font(&ctx, &f, &doc);

    ASSERT_NE(meta(MetaFontName), nullptr);
    ASSERT_STRNE(meta(MetaFontName)->str_val, "");
    ASSERT_NE(thumbnails_size(), 0);
    ASSERT_EQ(doc.thumbnail_count, 1);
}

TEST_F(FontTest, OpenType) {
    load("font/opentype1.otf");

    parse_font(&ctx, &f, &doc);

    ASSERT_NE(meta(MetaFontName), nullptr);
    ASSERT_NE(thumbnails_size(), 0);
}

TEST_F(FontTest, Woff) {
    load("font/woff.woff");

    parse_font(&ctx, &f, &doc);

    ASSERT_NE(meta(MetaFontName), nullptr);
}

TEST_F(FontTest, Woff2) {
    load("font/woff2.woff2");

    parse_font(&ctx, &f, &doc);

    ASSERT_NE(meta(MetaFontName), nullptr);
}

TEST_F(FontTest, ThumbnailDisabled) {
    ctx = make_font_ctx(FALSE);
    load("font/truetype1.ttf");

    parse_font(&ctx, &f, &doc);

    ASSERT_NE(meta(MetaFontName), nullptr);
    ASSERT_EQ(thumbnails_size(), 0);
}

/** A file that is not a font at all must be rejected without producing metadata */
TEST_F(FontTest, NotAFont) {
    load("text/books.csv");

    parse_font(&ctx, &f, &doc);

    ASSERT_EQ(meta(MetaFontName), nullptr);
    ASSERT_EQ(thumbnails_size(), 0);
}
