#include <gtest/gtest.h>

#include <set>
#include <string>

extern "C" {
#include "src/util.h"
}

/* str_escape() / str_unescape() — used to store text in the index and read it back */

TEST(StrEscape, AsciiIsUnchanged) {
    char escaped[256];

    str_escape(escaped, sizeof(escaped), "hello world");

    ASSERT_STREQ(escaped, "hello world");
}

TEST(StrEscape, RoundTripUtf8) {
    char escaped[256];
    char unescaped[256];

    str_escape(escaped, sizeof(escaped), "最後測試 test");
    str_unescape(unescaped, escaped);

    ASSERT_STREQ(unescaped, "最後測試 test");
}

TEST(StrEscape, InvalidUtf8IsEscaped) {
    char escaped[256];

    str_escape(escaped, sizeof(escaped), "test\xE0 test");

    // The invalid byte is replaced by an escape sequence, the valid text is kept
    ASSERT_TRUE(strstr(escaped, "test") != nullptr);
    ASSERT_EQ(strstr(escaped, "\xE0"), nullptr);
}

TEST(StrEscape, EmptyString) {
    char escaped[16];

    str_escape(escaped, sizeof(escaped), "");

    ASSERT_STREQ(escaped, "");
}

TEST(StrEscape, SizeCoversTheWorstCase) {
    // Every byte of an invalid sequence is escaped on its own, the longest expansion there is
    const std::string invalid(4096, '\xE0');

    const size_t size = str_escape_size(invalid.c_str());
    char *escaped = (char *) malloc(size);

    str_escape(escaped, size, invalid.c_str());

    ASSERT_EQ(strlen(escaped), invalid.size() * 3);
    free(escaped);
}

TEST(StrEscape, TruncatesInsteadOfOverflowing) {
    // A path made of invalid bytes, escaped into a buffer far too small to hold it
    const std::string invalid(4096, '\xE0');

    char escaped[32];
    memset(escaped, 'x', sizeof(escaped));

    str_escape(escaped, 16, invalid.c_str());

    // Nothing was written past the size it was given, and the result is still terminated
    ASSERT_LT(strlen(escaped), (size_t) 16);
    for (size_t i = 16; i < sizeof(escaped); i++) {
        ASSERT_EQ(escaped[i], 'x');
    }
}

TEST(StrEscape, TruncationKeepsSequencesWhole) {
    char escaped[8];

    // Room for one "]E0" and the terminator, but not for the second sequence's three bytes
    str_escape(escaped, 6, "\xE0\xE0");

    ASSERT_STREQ(escaped, "]E0");
}

/* hex2buf() / buf2hex() */

TEST(HexBuf, RoundTrip) {
    const unsigned char bytes[] = {0x00, 0x01, 0xfe, 0xff, 0x7a};
    char hex[sizeof(bytes) * 2 + 1];
    unsigned char parsed[sizeof(bytes)];

    buf2hex(bytes, sizeof(bytes), hex);

    ASSERT_STREQ(hex, "0001feff7a");
    ASSERT_TRUE(hex2buf(hex, sizeof(bytes) * 2, parsed));
    ASSERT_EQ(memcmp(bytes, parsed, sizeof(bytes)), 0);
}

TEST(HexBuf, UppercaseHexIsAccepted) {
    unsigned char parsed[2];

    ASSERT_TRUE(hex2buf("FEff", 4, parsed));
    ASSERT_EQ(parsed[0], 0xfe);
    ASSERT_EQ(parsed[1], 0xff);
}

TEST(HexBuf, NonHexCharactersDecodeToZero) {
    unsigned char parsed[2];

    ASSERT_TRUE(hex2buf("zzff", 4, parsed));
    ASSERT_EQ(parsed[0], 0x00);
    ASSERT_EQ(parsed[1], 0xff);
}

/* sist_id_t: "<index_id>.<doc_id>", both 8 hex digits */

TEST(ParseSid, ValidSid) {
    sist_id_t sid;

    ASSERT_TRUE(parse_sid(&sid, "0000000a.0000000b"));
    ASSERT_EQ(sid.index_id, 0xa);
    ASSERT_EQ(sid.doc_id, 0xb);
    ASSERT_STREQ(sid.sid_str, "0000000a.0000000b");
    ASSERT_EQ(sid.sid_int64, ((long) 0xa << 32) | 0xb);
}

TEST(ParseSid, MissingSeparatorIsRejected) {
    sist_id_t sid;

    ASSERT_FALSE(parse_sid(&sid, "0000000a-0000000b"));
}

TEST(ParseSid, RoundTripWithFormatSid) {
    char formatted[SIST_SID_LEN];
    sist_id_t sid;

    format_sid(formatted, 0x1234abcd, 0x0000ffff);

    ASSERT_TRUE(parse_sid(&sid, formatted));
    ASSERT_EQ((unsigned int) sid.index_id, 0x1234abcd);
    ASSERT_EQ(sid.doc_id, 0xffff);
}

/* Paths */

TEST(Path, AbspathResolvesRelativePath) {
    char *abs = abspath("/tmp/../tmp");

    ASSERT_NE(abs, nullptr);
    ASSERT_STREQ(abs, "/tmp");

    free(abs);
}

TEST(Path, AbspathReturnsNullForMissingPath) {
    char *abs = abspath("/this/path/does/not/exist/hopefully");

    ASSERT_EQ(abs, nullptr);
}

TEST(Path, ExpandpathExpandsHome) {
    setenv("HOME", "/home/sist2-test", TRUE);

    char *expanded = expandpath("~/documents");

    ASSERT_NE(expanded, nullptr);
    ASSERT_STREQ(expanded, "/home/sist2-test/documents");

    free(expanded);
}

/* URL escaping in document paths */

TEST(UrlEscape, HashIsEscaped) {
    char path[] = "archive.zip#/file.txt";

    dyn_buffer_t escaped = url_escape(path);

    ASSERT_STREQ(escaped.buf, "archive.zip%23/file.txt");

    dyn_buffer_destroy(&escaped);
}

TEST(UrlEscape, PlainPathIsUnchanged) {
    char path[] = "folder/file.txt";

    dyn_buffer_t escaped = url_escape(path);

    ASSERT_STREQ(escaped.buf, "folder/file.txt");

    dyn_buffer_destroy(&escaped);
}

/* Timers */

TEST(Timespec, AddMicroseconds) {
    struct timespec ts = {};
    ts.tv_sec = 10;

    struct timespec result = timespec_add(ts, 1500000);

    ASSERT_EQ(result.tv_sec, 11);
    ASSERT_EQ(result.tv_nsec, 500000000);
}

TEST(Timespec, AddWrapsNanoseconds) {
    struct timespec ts = {};
    ts.tv_sec = 1;
    ts.tv_nsec = 999000000;

    struct timespec result = timespec_add(ts, 2000);

    ASSERT_EQ(result.tv_sec, 2);
    ASSERT_EQ(result.tv_nsec, 1000000);
}

/* random_index_id() — two indices created within the same second must not share an id */

TEST(RandomIndexId, IsPositive) {
    for (int i = 0; i < 1000; i++) {
        ASSERT_GT(random_index_id(), 0);
    }
}

TEST(RandomIndexId, DoesNotRepeat) {
    std::set<int> ids;

    for (int i = 0; i < 1000; i++) {
        ids.insert(random_index_id());
    }

    ASSERT_EQ(ids.size(), 1000);
}
