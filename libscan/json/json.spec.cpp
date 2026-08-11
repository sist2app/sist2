#include "tests/support/scan_fixture.h"

class JsonTest : public ScanTest {
protected:
    scan_json_ctx_t ctx = make_json_ctx();
};

TEST_F(JsonTest, Json1) {
    load("json/json1.json");

    parse_json(&ctx, &f, &doc);

    ASSERT_NE(meta(MetaContent), nullptr);
}

TEST_F(JsonTest, NDJson1) {
    load("json/ndjson1.jsonl");

    parse_ndjson(&ctx, &f, &doc);

    ASSERT_NE(meta(MetaContent), nullptr);
}

TEST_F(JsonTest, MemInvalidJson) {
    const char *text = "{\"key\": ";
    load_mem(text, strlen(text));

    parse_json(&ctx, &f, &doc);
}

TEST_F(JsonTest, MemStringValuesAreIndexed) {
    const char *text = R"({"key": "hello", "nested": {"key2": "world"}})";
    load_mem(text, strlen(text));

    parse_json(&ctx, &f, &doc);

    ASSERT_TRUE(strstr(content(), "hello") != nullptr);
    ASSERT_TRUE(strstr(content(), "world") != nullptr);
}

TEST_F(JsonTest, MemNdjsonLines) {
    const char *text = "{\"key\": \"line1\"}\n{\"key\": \"line2\"}\n";
    load_mem(text, strlen(text));

    parse_ndjson(&ctx, &f, &doc);

    ASSERT_TRUE(strstr(content(), "line1") != nullptr);
    ASSERT_TRUE(strstr(content(), "line2") != nullptr);
}
