#include <gtest/gtest.h>

#include <string>

extern "C" {
#include <cjson/cJSON.h>

#include "libscan/scan.h"
#include "src/io/serialize.h"

/** The bundled schema/mappings.json, embedded into the binary by elastic.c */
extern const char mappings_json[];
}

/*
 * Elasticsearch maps a field it was not told about by guessing from the first value it sees. A
 * string that parses as a date makes the field a date, and every later value that does not parse
 * gets the whole document rejected - which is how one photo can keep the rest out of the index.
 */

class ElasticMappingsTest : public ::testing::Test {
protected:
    cJSON *mappings = nullptr;

    void SetUp() override {
        mappings = cJSON_Parse(mappings_json);
        ASSERT_NE(mappings, nullptr);
    }

    void TearDown() override {
        cJSON_Delete(mappings);
    }
};

/** Every field a document can carry is mapped, so nothing is left for Elasticsearch to guess */
TEST_F(ElasticMappingsTest, EveryFieldOfADocumentIsMapped) {
    cJSON *properties = cJSON_GetObjectItem(mappings, "properties");
    ASSERT_NE(properties, nullptr);

    for (int key = MetaContent; key < MetaThumbnail; key++) {
        const char *field = get_meta_key_text(static_cast<enum metakey>(key));

        EXPECT_NE(cJSON_GetObjectItem(properties, field), nullptr)
                            << "schema/mappings.json has no mapping for the '" << field << "' field";
    }
}

/** A field that is not mapped must not become a date behind our back either */
TEST_F(ElasticMappingsTest, DateDetectionIsOff) {
    cJSON *date_detection = cJSON_GetObjectItem(mappings, "date_detection");

    ASSERT_NE(date_detection, nullptr);
    ASSERT_TRUE(cJSON_IsFalse(date_detection));
}
