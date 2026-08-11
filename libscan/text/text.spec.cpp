#include "tests/support/scan_fixture.h"

class TextTest : public ScanTest {
protected:
    scan_text_ctx_t ctx = make_text_ctx();
};

TEST_F(TextTest, BookCsvContentLen) {
    load("text/books.csv");

    parse_text(&ctx, &f, &doc);

    ASSERT_NEAR(content_len(), 500, 4);
}

TEST_F(TextTest, MemUtf8_1) {
    const char *text = "a";
    load_mem(text, strlen(text));

    parse_text(&ctx, &f, &doc);

    ASSERT_EQ(meta(MetaContent), nullptr);
}

TEST_F(TextTest, MemUtf8_Invalid1) {
    const char *text = "12345\xE0";
    load_mem(text, strlen(text));

    parse_text(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "12345");
}

TEST_F(TextTest, MemUtf8_2) {
    const char *text = "最後測試";
    load_mem(text, strlen(text));

    parse_text(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "最後測試");
}

TEST_F(TextTest, MemUtf8_Invalid2) {
    const char *text = "最後測\xe8\xa9";
    load_mem(text, strlen(text));

    parse_text(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "最後測");
}

TEST_F(TextTest, MemWhitespace) {
    const char *text = "\n \ttest\t\ntest test     ";
    load_mem(text, strlen(text));

    parse_text(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "test test test");
}

TEST_F(TextTest, MemEmpty) {
    load_mem("", 0);

    parse_text(&ctx, &f, &doc);

    ASSERT_EQ(meta(MetaContent), nullptr);
}

TEST_F(TextTest, ContentSizeIsRespected) {
    ctx = make_text_ctx(20);
    load("text/books.csv");

    parse_text(&ctx, &f, &doc);

    ASSERT_NEAR(content_len(), 20, 4);
}

TEST_F(TextTest, Utf16LE) {
    load("text/pain_is_beauty.log");

    parse_text(&ctx, &f, &doc);

    ASSERT_GE(content_len(), 200);
}

TEST_F(TextTest, MemNoise) {
    char noise[600];

    for (char &i: noise) {
        int x = rand();
        i = x == 0 ? 1 : (char) x;
    }
    noise[599] = '\0';

    load_mem(noise, strlen(noise));

    parse_text(&ctx, &f, &doc);

    ASSERT_TRUE(utf8valid(content()) == 0);
}

class TextMarkupTest : public TextTest {
};

TEST_F(TextMarkupTest, Mem1) {
    const char *text = "<<a<aa<<<>test<aaaa><>test test    <>";
    load_mem(text, strlen(text));

    parse_markup(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "test test test");
}

TEST_F(TextMarkupTest, Mem2) {
    const char *text = "<<a<aa<<<>test<aaaa><>test test    ";
    load_mem(text, strlen(text));

    parse_markup(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "test test test");
}

TEST_F(TextMarkupTest, MemNoTags) {
    const char *text = "test test test";
    load_mem(text, strlen(text));

    parse_markup(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "test test test");
}

/** A tag that is never closed is not treated as markup; its contents end up in the text */
TEST_F(TextMarkupTest, MemUnclosedTag) {
    const char *text = "test <a href=\"http://example.com\" test";
    load_mem(text, strlen(text));

    parse_markup(&ctx, &f, &doc);

    ASSERT_STREQ(content(), "test a href http://example.com test");
}

TEST_F(TextMarkupTest, Xml1) {
    load("text/utf8-example.xml");

    parse_markup(&ctx, &f, &doc);

    ASSERT_NEAR(content_len(), 500, 4);
    ASSERT_TRUE(strstr(content(), " BMP:𐌈 ") != nullptr);
}
