#include "tests/support/scan_fixture.h"

#include <string>

extern "C" {
#include "libscan/util.h"
}

/** The text a buffer holds after appending each of those strings in turn */
static std::string append(const std::vector<std::string> &strings, long max_size = 4096) {
    text_buffer_t tex = text_buffer_create(max_size);

    for (const std::string &str: strings) {
        if (text_buffer_append_string(&tex, str.c_str(), str.size()) == TEXT_BUF_FULL) {
            break;
        }
    }
    text_buffer_terminate_string(&tex);

    std::string result(tex.dyn_buffer.buf, tex.dyn_buffer.cur - 1);
    text_buffer_destroy(&tex);

    return result;
}

/*
 * A separator appended on its own is what keeps the two strings around it apart, so it has to
 * become a space rather than be dropped — otherwise the words on either side are indexed as one.
 */
TEST(TextBufferTest, ShortSeparatorsAreKept) {
    ASSERT_EQ(append({"Subject", ": ", "a subject", "\n", "Date", ": ", "today"}),
              "Subject: a subject Date: today");
    ASSERT_EQ(append({"a word", "\n", "another"}), "a word another");
}

/** Whitespace is collapsed however it is split across the strings it was appended in */
TEST(TextBufferTest, WhitespaceIsCollapsed) {
    ASSERT_EQ(append({"one", " ", " ", "\n", "\t", "two"}), "one two");
    ASSERT_EQ(append({"one   \n\t  two"}), "one two");
}

/** A short string outside ASCII is text like any other */
TEST(TextBufferTest, ShortStringsOutsideAscii) {
    ASSERT_EQ(append({"caf", "é"}), "café");
    ASSERT_EQ(append({"é"}), "é");
}

TEST(TextBufferTest, MaxSizeIsRespected) {
    std::vector<std::string> strings;
    for (int i = 0; i < 1000; i++) {
        strings.emplace_back("ab");
    }

    ASSERT_LE(append(strings, 64).size(), 65u);
}

TEST(TextBufferTest, EmptyStrings) {
    ASSERT_EQ(append({"", "a", "", "b", ""}), "ab");
    ASSERT_EQ(append({}), "");
}
