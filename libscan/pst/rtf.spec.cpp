#include "tests/support/scan_fixture.h"

#include <string>

extern "C" {
#include "libscan/pst/rtf.h"
}

/** Reads an RTF document the way a message body written in RTF is read */
static std::string to_text(const std::string &rtf, size_t max_size = 4096) {
    size_t size = 0;
    char *text = rtf_to_text(rtf.c_str(), rtf.size(), max_size, &size);

    std::string result(text, size);
    free(text);

    return result;
}

TEST(RtfTest, PlainText) {
    const std::string text = to_text(R"({\rtf1\ansi\deff0 Hello from a message body.})");

    ASSERT_EQ(text, "Hello from a message body.");
}

/** A font or colour table holds no readable text, and its entries are not part of the body */
TEST(RtfTest, TablesAreSkipped) {
    const std::string text = to_text(
            R"({\rtf1\ansi{\fonttbl{\f0\froman Times New Roman;}}{\colortbl;\red0\green0\blue0;})"
            R"(\f0 The body itself.})");

    ASSERT_EQ(text.find("Times New Roman"), std::string::npos);
    ASSERT_NE(text.find("The body itself."), std::string::npos);
}

/** Everything in a \* destination is markup for a reader that understands it, never body text */
TEST(RtfTest, IgnorableDestinationsAreSkipped) {
    const std::string text = to_text(
            R"({\rtf1\ansi{\*\generator Riched20 10.0;}Body after the generator.})");

    ASSERT_EQ(text.find("Riched20"), std::string::npos);
    ASSERT_EQ(text, "Body after the generator.");
}

/*
 * Outlook stores an HTML message as RTF with the tags in \*\htmltag destinations, so dropping
 * those destinations is what leaves the text of the message behind.
 */
TEST(RtfTest, EncapsulatedHtmlKeepsOnlyTheText) {
    const std::string text = to_text(
            R"({\rtf1\ansi\fromhtml1{\*\htmltag19 <p>}Paragraph text{\*\htmltag19 </p>}})");

    ASSERT_EQ(text.find("<p>"), std::string::npos);
    ASSERT_EQ(text, "Paragraph text");
}

TEST(RtfTest, ParagraphsAreSeparated) {
    const std::string text = to_text(R"({\rtf1\ansi First line\par Second line})");

    ASSERT_NE(text.find("First line"), std::string::npos);
    ASSERT_NE(text.find("Second line"), std::string::npos);
}

/* An escaped brace is text rather than a group, so what follows it is still part of the body.
 * The braces themselves are not characters a search runs on, and the text buffer drops them. */
TEST(RtfTest, EscapedBracesDoNotOpenGroups) {
    const std::string text = to_text(R"({\rtf1\ansi A \{brace\} and text after it.})");

    ASSERT_NE(text.find("brace"), std::string::npos);
    ASSERT_NE(text.find("and text after it."), std::string::npos);
}

/** \uN is the codepoint, followed by a substitute for readers that cannot write it */
TEST(RtfTest, UnicodeEscapes) {
    const std::string text = to_text(R"({\rtf1\ansi caf\u233?})");

    ASSERT_EQ(text, "café");
}

TEST(RtfTest, HexEscapes) {
    const std::string text = to_text(R"({\rtf1\ansi caf\'e9})");

    ASSERT_EQ(text, "café");
}

TEST(RtfTest, MaxSizeIsRespected) {
    std::string rtf = R"({\rtf1\ansi )";
    for (int i = 0; i < 1000; i++) {
        rtf += "word ";
    }
    rtf += "}";

    ASSERT_LE(to_text(rtf, 64).size(), 64u);
}

/** \ucN says how many characters stand in for a \uN escape, and Outlook writes \uc0 */
TEST(RtfTest, UnicodeFallbackCount) {
    ASSERT_EQ(to_text(R"({\rtf1\ansi\uc0 caf\u233 Xtra})"), "caféXtra");
    ASSERT_EQ(to_text(R"({\rtf1\ansi\uc2 caf\u233 ??tra})"), "cafétra");
}

/** A body may hold raw 8bit bytes rather than the \'hh form, and they are latin-1 all the same */
TEST(RtfTest, RawEightBitBytes) {
    ASSERT_EQ(to_text("{\\rtf1\\ansi caf\xe9}"), "café");
}

/** A body made of escapes rather than literal text stops at max_size like any other */
TEST(RtfTest, MaxSizeIsRespectedForEscapes) {
    std::string hex = R"({\rtf1\ansi )";
    std::string words = R"({\rtf1\ansi )";
    for (int i = 0; i < 20000; i++) {
        hex += R"(\'41)";
        words += R"(word\par )";
    }
    hex += "}";
    words += "}";

    ASSERT_LE(to_text(hex, 1024).size(), 1024u);
    ASSERT_LE(to_text(words, 1024).size(), 1024u);
}

/** A document that ends in the middle of a group, a control word or an escape */
TEST(RtfTest, TruncatedDocument) {
    ASSERT_EQ(to_text(R"({\rtf1\ansi Text and then)"), "Text and then");
    ASSERT_NO_THROW(to_text(R"({\rtf1\ansi Text \)"));
    ASSERT_NO_THROW(to_text(R"({\rtf1\ansi Text \')"));
    ASSERT_NO_THROW(to_text(R"({\rtf1\ansi Text \u)"));
    ASSERT_NO_THROW(to_text("{{{{{{"));
    ASSERT_NO_THROW(to_text("}}}}}}"));
}

TEST(RtfTest, NotRtfAtAll) {
    ASSERT_NO_THROW(to_text(std::string("\x00\x01\x02\xff garbage", 12)));
}
