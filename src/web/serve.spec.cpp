#include <gtest/gtest.h>

#include <string>

extern "C" {
#include <cjson/cJSON.h>

#include "src/web/serve.h"
}

/*
 * A tag is whatever the user typed. It used to be pasted into the Elasticsearch _update body with
 * snprintf, so a quote or a backslash in a tag name produced a request Elasticsearch rejects.
 */

class TagScriptBodyTest : public ::testing::Test {
protected:
    /** The tag as Elasticsearch would read it back out of the request */
    std::string tag_of(const std::string &name) {
        char *body = tag_script_body("ctx._source.tag.add(params.tag)", name.c_str());

        cJSON *json = cJSON_Parse(body);
        free(body);

        if (json == nullptr) {
            return "<unparseable>";
        }

        cJSON *params = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "script"), "params");
        const std::string tag = cJSON_GetObjectItem(params, "tag")->valuestring;

        cJSON_Delete(json);
        return tag;
    }
};

TEST_F(TagScriptBodyTest, PlainTagSurvives) {
    EXPECT_EQ(tag_of("holiday#00FF00"), "holiday#00FF00");
}

TEST_F(TagScriptBodyTest, QuotesAndBackslashesSurvive) {
    EXPECT_EQ(tag_of("say \"hi\""), "say \"hi\"");
    EXPECT_EQ(tag_of("back\\slash"), "back\\slash");
}

/** Ending the JSON string early would let the rest of the tag write its own fields */
TEST_F(TagScriptBodyTest, ATagCannotAddFieldsOfItsOwn) {
    char *body = tag_script_body("ctx._source.tag.add(params.tag)",
                                 "x\", \"upsert\": {\"tag\": [\"pwned\"]}, \"unused\": \"");

    cJSON *json = cJSON_Parse(body);
    free(body);

    ASSERT_NE(json, nullptr);
    EXPECT_EQ(cJSON_GetObjectItem(json, "upsert"), nullptr);
    EXPECT_EQ(cJSON_GetArraySize(json), 1);

    cJSON_Delete(json);
}

TEST_F(TagScriptBodyTest, NewlinesAndControlCharactersSurvive) {
    EXPECT_EQ(tag_of("two\nlines"), "two\nlines");
}
