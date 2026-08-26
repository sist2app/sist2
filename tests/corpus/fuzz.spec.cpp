#include "tests/support/scan_fixture.h"

#include <vector>

/*
 * Feeds mangled copies of a real file to a parser. Run more rounds with:
 *   ./build/scan_a_test --gtest_filter=*Fuzz* --gtest_repeat=100
 */

#define FUZZ_ROUNDS 50

namespace {

    using ParseFn = void (*)(vfile_t *f, document_t *doc);

    struct FuzzCase {
        const char *name;
        const char *path;
        ParseFn parse;
    };

    void parse_text_case(vfile_t *f, document_t *doc) {
        scan_text_ctx_t ctx = make_text_ctx();
        parse_text(&ctx, f, doc);
    }

    void parse_markup_case(vfile_t *f, document_t *doc) {
        scan_text_ctx_t ctx = make_text_ctx();
        parse_markup(&ctx, f, doc);
    }

    void parse_json_case(vfile_t *f, document_t *doc) {
        scan_json_ctx_t ctx = make_json_ctx();
        parse_json(&ctx, f, doc);
    }

    void parse_ndjson_case(vfile_t *f, document_t *doc) {
        scan_json_ctx_t ctx = make_json_ctx();
        parse_ndjson(&ctx, f, doc);
    }

    void parse_font_case(vfile_t *f, document_t *doc) {
        scan_font_ctx_t ctx = make_font_ctx();
        parse_font(&ctx, f, doc);
    }

    void parse_email_case(vfile_t *f, document_t *doc) {
        scan_email_ctx_t ctx = make_email_ctx();
        parse_email(&ctx, f, doc);
    }

    void parse_mbox_case(vfile_t *f, document_t *doc) {
        scan_email_ctx_t ctx = make_email_ctx();
        parse_mbox(&ctx, f, doc);
    }

    void parse_heic_case(vfile_t *f, document_t *doc) {
        scan_media_ctx_t ctx = make_media_ctx(500, 0);
        parse_media(&ctx, f, doc, "image/heic");
    }

    void parse_mobi_case(vfile_t *f, document_t *doc) {
        scan_mobi_ctx_t ctx = make_mobi_ctx();
        parse_mobi(&ctx, f, doc);
    }

    const FuzzCase FUZZ_CASES[] = {
            {"Text",   "text/text.csv",         parse_text_case},
            {"Markup", "text/utf8-example.xml", parse_markup_case},
            {"Json",   "json/json1.json",       parse_json_case},
            {"NDJson", "json/ndjson1.jsonl",    parse_ndjson_case},
            {"Font",   "font/truetype1.ttf",    parse_font_case},
            {"Heic",   "media/tiled.heic",      parse_heic_case},
            {"Mobi",   "mobi/sample.azw3",      parse_mobi_case},
            {"Email",  "email/multipart.eml",   parse_email_case},
            {"Mbox",   "email/mailbox.mbox",    parse_mbox_case},
    };

    std::vector<char> read_test_file(const std::string &relative_path) {
        FILE *file = fopen(test_file(relative_path).c_str(), "rb");

        if (file == nullptr) {
            return {};
        }

        std::vector<char> buf;
        char chunk[8192];
        size_t read_bytes;

        while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0) {
            buf.insert(buf.end(), chunk, chunk + read_bytes);
        }

        fclose(file);

        return buf;
    }
}

class FuzzTest : public ScanTest, public ::testing::WithParamInterface<FuzzCase> {
};

TEST_P(FuzzTest, MangledInputDoesNotCrash) {
    const FuzzCase &fuzz_case = GetParam();

    const std::vector<char> original = read_test_file(fuzz_case.path);
    ASSERT_GT(original.size(), 2000) << "Fuzz input is missing or too small: " << fuzz_case.path;

    for (int round = 0; round < FUZZ_ROUNDS; round++) {
        std::vector<char> mangled = original;
        size_t len = mangled.size();

        fuzz_buffer(mangled.data(), &len, 4, 16, 5);

        reset();
        load_mem(mangled.data(), len);

        fuzz_case.parse(&f, &doc);
    }
}

INSTANTIATE_TEST_SUITE_P(
        Parsers, FuzzTest,
        ::testing::ValuesIn(FUZZ_CASES),
        [](const ::testing::TestParamInfo<FuzzCase> &info) { return info.param.name; });
