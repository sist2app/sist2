#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "tests/support/subprocess.h"

/*
 * Every format libscan can read needs a branch in get_file_type() to be reached at all. A parser
 * can keep passing its own tests while nothing dispatches to it: WordPerfect support was dropped
 * from parse.c in 43470e9 and went unnoticed for four years, because the files were still
 * recognized, indexed, and simply had no text.
 *
 * These run the real `sist2 scan` binary over one corpus file per parser and check that text came
 * out the other end.
 */

namespace fs = std::filesystem;

namespace {
    struct DispatchCase {
        const char *corpus_file;
        const char *mime;
    };

    const DispatchCase CASES[] = {
            {"wpd/test51_1.wpd",     "application/wordperfect"},
            {"msdoc/test1.doc",      "application/msword"},
            {"ebook/pdf1.pdf",       "application/pdf"},
            {"json/json1.json",      "application/json"},
            {"json/ndjson1.jsonl",   "application/ndjson"},
            {"mobi/sample.azw3",     "application/vnd.amazon.mobi8-ebook"},
            {"ooxml/docx2.docx",     "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    };
}

class ParseDispatchTest : public ::testing::TestWithParam<DispatchCase> {
protected:
    fs::path dir;
    fs::path index;

    void SetUp() override {
        dir = fs::temp_directory_path() / ("sist2-dispatch-spec-" + std::to_string(getpid()));
        index = dir / "index.sist2";

        fs::remove_all(dir);
        fs::create_directories(dir / "files");
    }

    void TearDown() override {
        fs::remove_all(dir);
    }

    bool copy_corpus_file(const std::string &relative) {
        const fs::path source = fs::path(SIST2_TEST_FILES_DIR) / relative;

        if (!fs::exists(source)) {
            return false;
        }

        fs::copy(source, dir / "files" / source.filename());
        return true;
    }

    int scan() {
        return sist2::test::run(std::string(SIST2_BINARY) + " scan --threads 1"
                                + " -o " + index.string()
                                + " " + (dir / "files").string()
                                + sist2::test::quiet());
    }

    /** Length of the extracted text of the single document in the index, or -1 */
    long long content_length() {
        sqlite3 *db;
        if (sqlite3_open(index.string().c_str(), &db) != SQLITE_OK) {
            return -1;
        }

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT length(coalesce(json_data ->> 'content', ''))"
                               " FROM document ORDER BY 1 DESC LIMIT 1", -1, &stmt, nullptr);

        const long long length = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int64(stmt, 0) : -1;

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return length;
    }

    /** First column of the first row, or "" */
    std::string scalar(const std::string &sql) {
        sqlite3 *db;
        if (sqlite3_open(index.string().c_str(), &db) != SQLITE_OK) {
            return "";
        }

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

        std::string value;
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0) != nullptr) {
            value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return value;
    }

    std::string mime_of_document() {
        sqlite3 *db;
        if (sqlite3_open(index.string().c_str(), &db) != SQLITE_OK) {
            return "";
        }

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT (SELECT name FROM mime WHERE id = document.mime)"
                               " FROM document ORDER BY id LIMIT 1", -1, &stmt, nullptr);

        std::string mime;
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0) != nullptr) {
            mime = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        return mime;
    }
};

TEST_P(ParseDispatchTest, TextIsExtracted) {
    const DispatchCase test_case = GetParam();

    if (!copy_corpus_file(test_case.corpus_file)) {
        GTEST_SKIP() << "Not in the test corpus: " << test_case.corpus_file;
    }

    ASSERT_EQ(scan(), 0);
    EXPECT_EQ(mime_of_document(), test_case.mime);
    EXPECT_GT(content_length(), 0);
}

/*
 * An ogg container is typed application/ogg whatever it holds, so the major mime of a video in one
 * says "application" and it used to reach no parser at all: no duration, no codec, no thumbnail.
 */
TEST_F(ParseDispatchTest, MediaUnderAnApplicationMimeIsParsed) {
    if (!copy_corpus_file("media/vid3.ogv")) {
        GTEST_SKIP() << "Not in the test corpus: media/vid3.ogv";
    }

    ASSERT_EQ(scan(), 0);

    EXPECT_EQ(mime_of_document(), "application/ogg");
    EXPECT_EQ(scalar("SELECT json_data ->> 'videoc' FROM document LIMIT 1"), "theora");
    EXPECT_NE(scalar("SELECT json_data ->> 'duration' FROM document LIMIT 1"), "");
    EXPECT_EQ(scalar("SELECT count(*) FROM thumbnail"), "1");
}

INSTANTIATE_TEST_SUITE_P(
        Dispatch, ParseDispatchTest, ::testing::ValuesIn(CASES),
        [](const ::testing::TestParamInfo<DispatchCase> &info) {
            std::string name = info.param.corpus_file;
            for (char &c: name) {
                if (!isalnum(c)) {
                    c = '_';
                }
            }
            return name;
        });
