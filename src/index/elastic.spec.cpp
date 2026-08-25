#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

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

/*
 * A key under "properties" is a field name, never a pattern: Elasticsearch only matches wildcards
 * inside dynamic_templates. An "emb.idx_384.*" property therefore maps nothing, and the vector a
 * user script writes is guessed as a float array, which [knn] refuses to search.
 */
TEST_F(ElasticMappingsTest, EmbeddingWildcardsAreDynamicTemplates) {
    const cJSON *properties = cJSON_GetObjectItem(mappings, "properties");
    ASSERT_NE(properties, nullptr);

    const cJSON *property;
    cJSON_ArrayForEach(property, properties) {
        const std::string name = property->string;
        if (name.rfind("emb.", 0) == 0) {
            EXPECT_EQ(name.find('*'), std::string::npos)
                                << name << " is a pattern, so it belongs in dynamic_templates";
        }
    }

    const cJSON *templates = cJSON_GetObjectItem(mappings, "dynamic_templates");
    ASSERT_NE(templates, nullptr) << "no dynamic_templates: no model but CLIP can be searched";

    int embedding_templates = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, templates) {
        const cJSON *body = entry->child;
        const cJSON *path_match = cJSON_GetObjectItem(body, "path_match");
        const cJSON *mapping = cJSON_GetObjectItem(body, "mapping");

        ASSERT_TRUE(cJSON_IsString(path_match)) << body->string;
        ASSERT_NE(mapping, nullptr) << body->string;

        if (std::string(path_match->valuestring).rfind("emb.", 0) != 0) {
            continue;
        }
        embedding_templates += 1;

        const cJSON *type = cJSON_GetObjectItem(mapping, "type");
        ASSERT_TRUE(cJSON_IsString(type)) << path_match->valuestring;
        EXPECT_STREQ(type->valuestring, "dense_vector") << path_match->valuestring;
        EXPECT_TRUE(cJSON_IsNumber(cJSON_GetObjectItem(mapping, "dims")))
                            << path_match->valuestring << " has no dims";
    }

    EXPECT_GT(embedding_templates, 0);
}

/** Every size a user script may register a model with needs a template that indexes it for knn */
TEST_F(ElasticMappingsTest, EverySearchableEmbeddingSizeIsIndexed) {
    const cJSON *templates = cJSON_GetObjectItem(mappings, "dynamic_templates");
    ASSERT_NE(templates, nullptr);

    for (const int dims: {384, 512, 768, 1024}) {
        const std::string wanted = "emb.idx_" + std::to_string(dims) + ".*";

        bool found = false;
        const cJSON *entry;
        cJSON_ArrayForEach(entry, templates) {
            const cJSON *body = entry->child;
            const cJSON *path_match = cJSON_GetObjectItem(body, "path_match");

            if (cJSON_IsString(path_match) && wanted == path_match->valuestring) {
                const cJSON *mapping = cJSON_GetObjectItem(body, "mapping");
                EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(mapping, "index"))) << wanted;
                EXPECT_EQ(cJSON_GetObjectItem(mapping, "dims")->valueint, dims) << wanted;
                found = true;
            }
        }

        EXPECT_TRUE(found) << "no dynamic template for " << wanted;
    }
}

/** The chunk vectors are searched with [knn], which needs the object holding them to be nested */
TEST_F(ElasticMappingsTest, ChunkEmbeddingsAreNested) {
    const cJSON *properties = cJSON_GetObjectItem(mappings, "properties");
    ASSERT_NE(properties, nullptr);

    const cJSON *chunks = cJSON_GetObjectItem(properties, "emb_chunks");
    ASSERT_NE(chunks, nullptr) << "no emb_chunks mapping: an embeddings search has no excerpt";

    const cJSON *type = cJSON_GetObjectItem(chunks, "type");
    ASSERT_TRUE(cJSON_IsString(type));
    EXPECT_STREQ(type->valuestring, "nested");

    const cJSON *chunk_properties = cJSON_GetObjectItem(chunks, "properties");
    ASSERT_NE(chunk_properties, nullptr);

    // The passage the inner hit quotes back, and where it sits in the text
    for (const char *field: {"start", "end", "text"}) {
        EXPECT_NE(cJSON_GetObjectItem(chunk_properties, field), nullptr) << field;
    }
}

/** A model that can be searched over whole documents can be searched over their chunks */
TEST_F(ElasticMappingsTest, EveryEmbeddingTemplateHasAChunkTwin) {
    const cJSON *templates = cJSON_GetObjectItem(mappings, "dynamic_templates");
    ASSERT_NE(templates, nullptr);

    std::vector<std::string> paths;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, templates) {
        const cJSON *path_match = cJSON_GetObjectItem(entry->child, "path_match");
        ASSERT_TRUE(cJSON_IsString(path_match));
        paths.emplace_back(path_match->valuestring);
    }

    for (const std::string &path: paths) {
        if (path.rfind("emb.", 0) != 0) {
            continue;
        }

        const std::string twin = "emb_chunks." + path;
        EXPECT_NE(std::find(paths.begin(), paths.end(), twin), paths.end())
                            << "no dynamic template for " << twin;
    }
}
