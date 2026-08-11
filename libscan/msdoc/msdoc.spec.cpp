#include "tests/support/scan_fixture.h"

class MsdocTest : public ScanTest {
protected:
    scan_msdoc_ctx_t ctx = make_msdoc_ctx();
};

TEST_F(MsdocTest, Test1) {
    load("msdoc/test1.doc");

    parse_msdoc(&ctx, &f, &doc);

    ASSERT_TRUE(strstr(content(), "October 2000") != nullptr);
    ASSERT_STREQ(meta(MetaTitle)->str_val, "INTERNATIONAL ORGANIZATION FOR STANDARDIZATION");
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Oliver Morgan");
    ASSERT_NEAR(content_len(), ctx.content_size, 4);
}

TEST_F(MsdocTest, Test2) {
    load("msdoc/test2.doc");

    parse_msdoc(&ctx, &f, &doc);

    ASSERT_TRUE(strstr(content(), "GNU Free Documentation License") != nullptr);
    ASSERT_STREQ(meta(MetaTitle)->str_val, "DWARF Debugging Information Format");
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Ron Brender");
    ASSERT_NEAR(content_len(), ctx.content_size, 4);
}

TEST_F(MsdocTest, Test3) {
    load("msdoc/test3.doc");

    parse_msdoc(&ctx, &f, &doc);

    ASSERT_TRUE(strstr(content(), "INTERNATIONAL PATENT CLASSIFICATION") != nullptr);
    ASSERT_STREQ(meta(MetaTitle)->str_val, "IPC Fixed Texts Specification");
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Fievet");
    ASSERT_NEAR(content_len(), ctx.content_size, 4);
}

TEST_F(MsdocTest, Test4) {
    load("msdoc/test4.doc");

    parse_msdoc(&ctx, &f, &doc);

    ASSERT_TRUE(strstr(content(), "SQL Server international data types") != nullptr);
    ASSERT_STREQ(meta(MetaTitle)->str_val, "MSDN Authoring Template");
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "Brenda Yen");
    ASSERT_NEAR(content_len(), ctx.content_size, 4);
}

TEST_F(MsdocTest, Test5) {
    load("msdoc/test5.doc");

    parse_msdoc(&ctx, &f, &doc);

    ASSERT_TRUE(strstr(content(), "орган Федеральной") != nullptr);
    ASSERT_STREQ(meta(MetaAuthor)->str_val, "uswo");
    ASSERT_NEAR(content_len(), ctx.content_size, 4);
}

TEST_F(MsdocTest, Utf8) {
    load("msdoc/japanese.doc");

    parse_msdoc(&ctx, &f, &doc);

    ASSERT_NE(meta(MetaContent), nullptr);
    ASSERT_TRUE(strstr(content(), "调查项目 A questionnaire") != nullptr);
}

/** A truncated or corrupted OLE document must not crash the parser */
TEST_F(MsdocTest, Fuzz1) {
    load("msdoc/fuzz_ole.doc");

    size_t buf_len;
    char *buf = (char *) read_all(&f, &buf_len);
    ASSERT_NE(buf, nullptr);

    for (int i = 0; i < 1000; i++) {
        size_t fuzzed_len = buf_len;
        char *fuzzed = (char *) malloc(buf_len);
        memcpy(fuzzed, buf, buf_len);

        fuzz_buffer(fuzzed, &fuzzed_len, 3, 8, 5);

        // parse_msdoc_text() takes ownership of the buffer and frees it
        FILE *file = fmemopen(fuzzed, fuzzed_len, "rb");
        parse_msdoc_text(&ctx, &doc, file, fuzzed, fuzzed_len);
    }

    free(buf);
}
