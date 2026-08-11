#include "tests/support/scan_fixture.h"

class MobiTest : public ScanTest {
protected:
    scan_mobi_ctx_t ctx = make_mobi_ctx();
};

TEST_F(MobiTest, Mobi1) {
    load("mobi/Norse Mythology - Neil Gaiman.mobi");

    parse_mobi(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Gaiman, Neil");
    ASSERT_STREQ(meta(MetaTitle)->str_val, "Norse Mythology");
    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(MobiTest, Azw) {
    load("mobi/sample.azw");

    parse_mobi(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Nietzsche, Friedrich");
    ASSERT_STREQ(meta(MetaTitle)->str_val, "On the Genealogy of Morality (Hackett Classics)");
    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(MobiTest, Azw3) {
    load("mobi/sample.azw3");

    parse_mobi(&ctx, &f, &doc);

    ASSERT_STREQ(meta(MetaAuthor)->str_val, "George Orwell; Amélie Audiberti");
    ASSERT_STREQ(meta(MetaTitle)->str_val, "1984");
    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(MobiTest, ContentSizeIsRespected) {
    ctx = make_mobi_ctx(100);
    load("mobi/sample.azw3");

    parse_mobi(&ctx, &f, &doc);

    ASSERT_NEAR(content_len(), 100, 4);
}
