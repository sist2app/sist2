#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern "C" {
#include "src/io/serialize.h"
#include "src/worker/sink.h"
#include "src/ctx.h"
}

/*
 * A document is serialized to the JSON that reaches Elasticsearch and the index database, so a
 * field written twice is not a cosmetic problem: Elasticsearch refuses the whole document with
 * "Duplicate field".
 */

namespace {
    std::string written_json;

    int capture_mark_document(const char *, int) {
        return FALSE;
    }

    void capture_write_document(document_t *, const char *json) {
        written_json = json;
    }

    void capture_write_thumbnail(int, const void *, size_t) {}

    void capture_set_current_job(const char *) {}

    const document_sink_t CaptureSink = {
            .mark_document = capture_mark_document,
            .write_document = capture_write_document,
            .write_thumbnail = capture_write_thumbnail,
            .set_current_job = capture_set_current_job,
    };
}

class SerializeTest : public ::testing::Test {
protected:
    void SetUp() override {
        written_json.clear();
        DocumentSink = &CaptureSink;

        strcpy(ScanCtx.index.desc.root, "/root/");
        ScanCtx.index.desc.root_len = (int) strlen("/root/");
    }

    /** A document with a name and an extension, ready for meta lines to be appended */
    static document_t *make_document() {
        auto *doc = (document_t *) calloc(1, sizeof(document_t));

        strcpy(doc->filepath, "/root/folder/file.txt");
        doc->base = (short) strlen("/root/folder/");
        doc->ext = (short) strlen("/root/folder/file.");

        return doc;
    }

    static void append_string_meta(document_t *doc, enum metakey key, const char *value) {
        auto *meta = (meta_line_t *) malloc(sizeof(meta_line_t) + strlen(value) + 1);
        meta->key = key;
        strcpy(meta->str_val, value);

        APPEND_META(doc, meta);
    }

    static int count_occurrences(const std::string &haystack, const std::string &needle) {
        int count = 0;
        for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
            count += 1;
        }
        return count;
    }
};

/** Text found in more than one place in a file belongs to the same document, so it is kept */
TEST_F(SerializeTest, TextFoundTwiceEndsUpInOneField) {
    document_t *doc = make_document();
    append_string_meta(doc, MetaContent, "subtitles of the video");
    append_string_meta(doc, MetaContent, "text read off the picture");

    write_document(doc);

    ASSERT_EQ(count_occurrences(written_json, "\"content\""), 1);
    ASSERT_NE(written_json.find("subtitles of the video"), std::string::npos);
    ASSERT_NE(written_json.find("text read off the picture"), std::string::npos);
}

/** Anything else is a single value, and the first one found wins */
TEST_F(SerializeTest, AFieldThatIsNotTextIsWrittenOnce) {
    document_t *doc = make_document();
    append_string_meta(doc, MetaTitle, "the real title");
    append_string_meta(doc, MetaTitle, "another title");

    write_document(doc);

    ASSERT_EQ(count_occurrences(written_json, "\"title\""), 1);
    ASSERT_NE(written_json.find("the real title"), std::string::npos);
    ASSERT_EQ(written_json.find("another title"), std::string::npos);
}
