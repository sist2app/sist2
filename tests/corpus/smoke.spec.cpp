#include "tests/support/scan_fixture.h"

#include <algorithm>
#include <map>

/*
 * Every file of the test corpus is parsed by the parser its extension maps to. The assertions are
 * deliberately weak — the point is that no corpus file crashes, hangs, leaks (scan_a_test) or
 * triggers undefined behaviour (scan_ub_test), whichever parser it lands in. Dropping a new file
 * into the corpus is enough to cover it.
 */

namespace {

    std::string extension_of(const std::string &path) {
        const size_t dot = path.find_last_of('.');
        const size_t slash = path.find_last_of('/');

        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
            return "";
        }

        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        return ext;
    }

    const std::map<std::string, const char *> MEDIA_MIMES = {
            {"jpg",  "image/jpeg"},
            {"jpeg", "image/jpeg"},
            {"png",  "image/png"},
            {"gif",  "image/gif"},
            {"bmp",  "image/bmp"},
            {"webp", "image/webp"},
            {"heic", "image/heic"},
            {"mkv",  "video/x-matroska"},
            {"mp4",  "video/mp4"},
            {"webm", "video/webm"},
            {"ogv",  "application/ogg"},
            {"avi",  "video/x-msvideo"},
            {"mov",  "video/quicktime"},
            {"mp3",  "audio/x-mpeg-3"},
            {"flac", "audio/flac"},
            {"ogg",  "audio/ogg"},
            {"wav",  "audio/x-wav"},
    };

    const std::map<std::string, const char *> EBOOK_MIMES = {
            {"pdf",  "application/pdf"},
            {"epub", "application/epub+zip"},
    };

    bool is_one_of(const std::string &ext, std::initializer_list<const char *> extensions) {
        return std::any_of(extensions.begin(), extensions.end(),
                           [&ext](const char *candidate) { return ext == candidate; });
    }
}

class CorpusSmokeTest : public ArcTest, public ::testing::WithParamInterface<std::string> {
protected:
    void parse_by_extension(const std::string &ext);
};

void CorpusSmokeTest::parse_by_extension(const std::string &ext) {
    if (MEDIA_MIMES.count(ext)) {
        scan_media_ctx_t ctx = make_media_ctx();
        parse_media(&ctx, &f, &doc, MEDIA_MIMES.at(ext));

    } else if (EBOOK_MIMES.count(ext)) {
        scan_ebook_ctx_t ctx = make_ebook_ctx();
        parse_ebook(&ctx, &f, EBOOK_MIMES.at(ext), &doc);

    } else if (is_one_of(ext, {"cbz", "cbr"})) {
        scan_comic_ctx_t ctx = make_comic_ctx();
        parse_comic(&ctx, &f, &doc);

    } else if (is_one_of(ext, {"docx", "xlsx", "pptx"})) {
        scan_ooxml_ctx_t ctx = make_ooxml_ctx();
        parse_ooxml(&ctx, &f, &doc);

    } else if (is_one_of(ext, {"mobi", "azw", "azw3"})) {
        scan_mobi_ctx_t ctx = make_mobi_ctx();
        parse_mobi(&ctx, &f, &doc);

    } else if (ext == "doc") {
        scan_msdoc_ctx_t ctx = make_msdoc_ctx();
        parse_msdoc(&ctx, &f, &doc);

    } else if (ext == "wpd") {
        scan_wpd_ctx_t ctx = make_wpd_ctx();
        parse_wpd(&ctx, &f, &doc);

    } else if (ext == "json") {
        scan_json_ctx_t ctx = make_json_ctx();
        parse_json(&ctx, &f, &doc);

    } else if (is_one_of(ext, {"jsonl", "ndjson"})) {
        scan_json_ctx_t ctx = make_json_ctx();
        parse_ndjson(&ctx, &f, &doc);

    } else if (is_one_of(ext, {"ttf", "otf", "woff", "woff2"})) {
        scan_font_ctx_t ctx = make_font_ctx();
        parse_font(&ctx, &f, &doc);

    } else if (is_one_of(ext, {"dng", "rw2", "nef", "arw", "orf", "raf", "cr2", "cr3", "pef", "srw"})) {
        scan_raw_ctx_t ctx = make_raw_ctx();
        parse_raw(&ctx, &f, &doc);

    } else if (is_one_of(ext, {"zip", "tar", "7z", "rar", "gz", "tgz", "bz2", "xz", "jar", "cbt"})) {
        scan_arc_ctx_t ctx = make_arc_ctx(ARC_MODE_LIST);
        parse_archive(&ctx, &f, &doc, nullptr, nullptr);

    } else if (is_one_of(ext, {"html", "htm", "xml", "svg"})) {
        scan_text_ctx_t ctx = make_text_ctx();
        parse_markup(&ctx, &f, &doc);

    } else {
        scan_text_ctx_t ctx = make_text_ctx();
        parse_text(&ctx, &f, &doc);
    }
}

TEST_P(CorpusSmokeTest, Parses) {
    const std::string &relative_path = GetParam();

    load(relative_path);

    parse_by_extension(extension_of(relative_path));

    // A key found twice is serialized as the same JSON field twice, which Elasticsearch rejects.
    // Thumbnails are the one meta line a document is allowed to repeat.
    std::map<int, int> keys;
    for (meta_line_t *meta = doc.meta_head; meta != nullptr; meta = meta->next) {
        if (meta->key != MetaThumbnail) {
            keys[meta->key] += 1;
        }
    }

    for (const auto &[key, count]: keys) {
        EXPECT_EQ(count, 1) << "meta key " << key << " was produced " << count << " times";
    }
}

INSTANTIATE_TEST_SUITE_P(
        Corpus, CorpusSmokeTest,
        ::testing::ValuesIn(corpus_files()),
        [](const ::testing::TestParamInfo<std::string> &info) {
            std::string name = info.param;

            // Test names may only contain alphanumeric characters and underscores
            for (char &c: name) {
                if (!isalnum(static_cast<unsigned char>(c))) {
                    c = '_';
                }
            }

            return std::to_string(info.index) + "_" + name;
        });
